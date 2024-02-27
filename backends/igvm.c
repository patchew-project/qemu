/*
 * QEMU IGVM configuration backend for Confidential Guests
 *
 * Copyright (C) 2023-2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@suse.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#if defined(CONFIG_IGVM)

#include "exec/confidential-guest-support.h"
#include "qemu/queue.h"
#include "qemu/typedefs.h"

#include "exec/igvm.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"

#include <igvm/igvm.h>
#include <igvm/igvm_defs.h>
#include <linux/kvm.h>

typedef struct IgvmParameterData {
    QTAILQ_ENTRY(IgvmParameterData) next;
    uint8_t *data;
    uint32_t size;
    uint32_t index;
} IgvmParameterData;

static QTAILQ_HEAD(, IgvmParameterData) parameter_data;

static void directive_page_data(ConfidentialGuestSupport *cgs, int i,
                                uint32_t compatibility_mask);
static void directive_vp_context(ConfidentialGuestSupport *cgs, int i,
                                 uint32_t compatibility_mask);
static void directive_parameter_area(ConfidentialGuestSupport *cgs, int i,
                                     uint32_t compatibility_mask);
static void directive_parameter_insert(ConfidentialGuestSupport *cgs, int i,
                                       uint32_t compatibility_mask);
static void directive_memory_map(ConfidentialGuestSupport *cgs, int i,
                                 uint32_t compatibility_mask);
static void directive_vp_count(ConfidentialGuestSupport *cgs, int i,
                               uint32_t compatibility_mask);
static void directive_environment_info(ConfidentialGuestSupport *cgs, int i,
                                       uint32_t compatibility_mask);
static void directive_required_memory(ConfidentialGuestSupport *cgs, int i,
                                      uint32_t compatibility_mask);

struct IGVMDirectiveHandler {
    uint32_t type;
    void (*handler)(ConfidentialGuestSupport *cgs, int i,
                    uint32_t compatibility_mask);
};

static struct IGVMDirectiveHandler directive_handlers[] = {
    { IGVM_VHT_PAGE_DATA, directive_page_data },
    { IGVM_VHT_VP_CONTEXT, directive_vp_context },
    { IGVM_VHT_PARAMETER_AREA, directive_parameter_area },
    { IGVM_VHT_PARAMETER_INSERT, directive_parameter_insert },
    { IGVM_VHT_MEMORY_MAP, directive_memory_map },
    { IGVM_VHT_VP_COUNT_PARAMETER, directive_vp_count },
    { IGVM_VHT_ENVIRONMENT_INFO_PARAMETER, directive_environment_info },
    { IGVM_VHT_REQUIRED_MEMORY, directive_required_memory },
};

static void directive(uint32_t type, ConfidentialGuestSupport *cgs, int i,
                      uint32_t compatibility_mask)
{
    size_t handler;
    for (handler = 0; handler < (sizeof(directive_handlers) /
                                 sizeof(struct IGVMDirectiveHandler));
         ++handler) {
        if (directive_handlers[handler].type == type) {
            directive_handlers[handler].handler(cgs, i, compatibility_mask);
            return;
        }
    }
    warn_report("IGVM: Unknown directive encountered when processing file: %X",
                type);
}

static void igvm_handle_error(int32_t result, const char *msg)
{
    if (result < 0) {
        error_report("Processing of IGVM file failed: %s (code: %d)", msg,
                     (int)result);
        exit(EXIT_FAILURE);
    }
}

static void *igvm_prepare_memory(uint64_t addr, uint64_t size,
                                 int region_identifier)
{
    MemoryRegion *igvm_pages = NULL;
    Int128 gpa_region_size;
    MemoryRegionSection mrs =
        memory_region_find(get_system_memory(), addr, size);
    if (mrs.mr) {
        if (!memory_region_is_ram(mrs.mr)) {
            memory_region_unref(mrs.mr);
            error_report(
                "Processing of IGVM file failed: Could not prepare memory "
                "at address 0x%lX due to existing non-RAM region",
                addr);
            exit(EXIT_FAILURE);
        }

        gpa_region_size = int128_make64(size);
        if (int128_lt(mrs.size, gpa_region_size)) {
            memory_region_unref(mrs.mr);
            error_report(
                "Processing of IGVM file failed: Could not prepare memory "
                "at address 0x%lX: region size exceeded",
                addr);
            exit(EXIT_FAILURE);
        }
        return qemu_map_ram_ptr(mrs.mr->ram_block, mrs.offset_within_region);
    } else {
        /*
         * The region_identifier is the is the index of the IGVM directive that
         * contains the page with the lowest GPA in the region. This will
         * generate a unique region name.
         */
        char region_name[22];
        snprintf(region_name, sizeof(region_name), "igvm.%X",
                 region_identifier);
        igvm_pages = g_malloc(sizeof(*igvm_pages));
        memory_region_init_ram(igvm_pages, NULL, region_name, size,
                               &error_fatal);
        memory_region_add_subregion(get_system_memory(), addr, igvm_pages);
        return memory_region_get_ram_ptr(igvm_pages);
    }
}

static int igvm_type_to_cgs_type(IgvmPageDataType memory_type, bool unmeasured,
                                 bool zero)
{
    switch (memory_type) {
    case NORMAL: {
        if (unmeasured) {
            return CGS_PAGE_TYPE_UNMEASURED;
        } else {
            return zero ? CGS_PAGE_TYPE_ZERO :
                          CGS_PAGE_TYPE_NORMAL;
        }
    }
    case SECRETS:
        return CGS_PAGE_TYPE_SECRETS;
    case CPUID_DATA:
        return CGS_PAGE_TYPE_CPUID;
    case CPUID_XF:
        return CGS_PAGE_TYPE_CPUID;
    default:
        return CGS_PAGE_TYPE_UNMEASURED;
    }
}

static bool page_attrs_equal(const IGVM_VHS_PAGE_DATA *page_1,
                             const IGVM_VHS_PAGE_DATA *page_2)
{
    return ((*(const uint32_t *)&page_1->flags ==
             *(const uint32_t *)&page_2->flags) &&
            (page_1->data_type == page_2->data_type) &&
            (page_1->compatibility_mask == page_2->compatibility_mask));
}

static void igvm_process_mem_region(ConfidentialGuestSupport *cgs,
                                    IgvmHandle igvm, int start_index,
                                    uint64_t gpa_start, int page_count,
                                    const IgvmPageDataFlags *flags,
                                    const IgvmPageDataType page_type)
{
    uint8_t *region;
    IgvmHandle data_handle;
    const void *data;
    uint32_t data_size;
    int i;
    bool zero = true;
    const uint64_t page_size = flags->is_2mb_page ? 0x200000 : 0x1000;
    int result;

    region = igvm_prepare_memory(gpa_start, page_count * page_size,
                                 start_index);

    for (i = 0; i < page_count; ++i) {
        data_handle = igvm_get_header_data(igvm, HEADER_SECTION_DIRECTIVE,
                                           i + start_index);
        if (data_handle == IGVMAPI_NO_DATA) {
            /* No data indicates a zero page */
            memset(&region[i * page_size], 0, page_size);
        } else if (data_handle < 0) {
            igvm_handle_error(data_handle, "Invalid file");
        } else {
            zero = false;
            data = igvm_get_buffer(igvm, data_handle);
            data_size = igvm_get_buffer_size(igvm, data_handle);
            if (data_size < page_size) {
                memset(&region[i * page_size], 0, page_size);
            } else if (data_size > page_size) {
                igvm_handle_error(data_handle, "Invalid page data in file");
            }
            memcpy(&region[i * page_size], data, data_size);
            igvm_free_buffer(igvm, data_handle);
        }
    }

    result = cgs->set_guest_state(
        gpa_start, region, page_size * page_count,
        igvm_type_to_cgs_type(page_type, flags->unmeasured, zero), 0);
    if (result != 0) {
        error_report("IGVM set guest state failed with code %d", result);
        exit(EXIT_FAILURE);
    }
}

static void process_mem_page(ConfidentialGuestSupport *cgs, int i,
                             const IGVM_VHS_PAGE_DATA *page_data)
{
    static IGVM_VHS_PAGE_DATA prev_page_data;
    static uint64_t region_start;
    static int last_i;
    static int page_count;

    if (page_data) {
        if (page_count == 0) {
            region_start = page_data->gpa;
        } else {
            if (!page_attrs_equal(page_data, &prev_page_data) ||
                ((prev_page_data.gpa +
                  (prev_page_data.flags.is_2mb_page ? 0x200000 : 0x1000)) !=
                 page_data->gpa) ||
                (last_i != (i - 1))) {
                /* End of current region */
                igvm_process_mem_region(cgs, cgs->igvm, i - page_count,
                                        region_start, page_count,
                                        &prev_page_data.flags,
                                        prev_page_data.data_type);
                page_count = 0;
                region_start = page_data->gpa;
            }
        }
        memcpy(&prev_page_data, page_data, sizeof(prev_page_data));
        last_i = i;
        ++page_count;
    } else {
        if (page_count > 0) {
            igvm_process_mem_region(cgs, cgs->igvm, i - page_count,
                                    region_start, page_count,
                                    &prev_page_data.flags,
                                    prev_page_data.data_type);
            page_count = 0;
        }
    }
}

static void directive_page_data(ConfidentialGuestSupport *cgs, int i,
                                uint32_t compatibility_mask)
{
    IgvmHandle header_handle;
    const IGVM_VHS_PAGE_DATA *page_data;

    header_handle = igvm_get_header(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
    igvm_handle_error(header_handle,
                      "Failed to read directive header from file");
    page_data =
        (IGVM_VHS_PAGE_DATA *)(igvm_get_buffer(cgs->igvm, header_handle) +
                               sizeof(IGVM_VHS_VARIABLE_HEADER));

    if (page_data->compatibility_mask == compatibility_mask) {
        process_mem_page(cgs, i, page_data);
    }
    igvm_free_buffer(cgs->igvm, header_handle);
}

static void directive_vp_context(ConfidentialGuestSupport *cgs, int i,
                                 uint32_t compatibility_mask)
{
    IgvmHandle header_handle;
    IGVM_VHS_VP_CONTEXT *vp_context;
    IgvmHandle data_handle;
    uint8_t *data;
    int result;

    header_handle = igvm_get_header(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
    igvm_handle_error(header_handle,
                      "Failed to read directive header from file");
    vp_context =
        (IGVM_VHS_VP_CONTEXT *)(igvm_get_buffer(cgs->igvm, header_handle) +
                                sizeof(IGVM_VHS_VARIABLE_HEADER));

    if (vp_context->compatibility_mask == compatibility_mask) {
        data_handle =
            igvm_get_header_data(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
        igvm_handle_error(data_handle,
                          "Failed to read directive data from file");

        data = (uint8_t *)igvm_get_buffer(cgs->igvm, data_handle);
        result = cgs->set_guest_state(vp_context->gpa, data,
                             igvm_get_buffer_size(cgs->igvm, data_handle),
                             CGS_PAGE_TYPE_VMSA,
                             vp_context->vp_index);
        igvm_free_buffer(cgs->igvm, data_handle);
        if (result != 0) {
            error_report(
                "IGVM: Failed to set CPU context: error_code=%d",
                result);
            exit(EXIT_FAILURE);
        }

    }

    igvm_free_buffer(cgs->igvm, header_handle);
}

static void directive_parameter_area(ConfidentialGuestSupport *cgs, int i,
                                     uint32_t compatibility_mask)
{
    IgvmHandle header_handle;
    IGVM_VHS_PARAMETER_AREA *param_area;
    IgvmParameterData *param_entry;

    header_handle = igvm_get_header(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
    igvm_handle_error(header_handle,
                      "Failed to read directive header from file");
    param_area =
        (IGVM_VHS_PARAMETER_AREA *)(igvm_get_buffer(cgs->igvm, header_handle) +
                                    sizeof(IGVM_VHS_VARIABLE_HEADER));

    param_entry = g_new0(IgvmParameterData, 1);
    param_entry->size = param_area->number_of_bytes;
    param_entry->index = param_area->parameter_area_index;
    param_entry->data = g_malloc0(param_entry->size);

    QTAILQ_INSERT_TAIL(&parameter_data, param_entry, next);

    igvm_free_buffer(cgs->igvm, header_handle);
}

static void directive_parameter_insert(ConfidentialGuestSupport *cgs, int i,
                                       uint32_t compatibility_mask)
{
    IgvmHandle header_handle;
    IGVM_VHS_PARAMETER_INSERT *param;
    IgvmParameterData *param_entry;
    int result;
    void *region;

    header_handle = igvm_get_header(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
    igvm_handle_error(header_handle,
                      "Failed to read directive header from file");
    param = (IGVM_VHS_PARAMETER_INSERT *)(igvm_get_buffer(cgs->igvm,
                                                          header_handle) +
                                          sizeof(IGVM_VHS_VARIABLE_HEADER));

    QTAILQ_FOREACH(param_entry, &parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            region = igvm_prepare_memory(param->gpa, param_entry->size, i);
            if (!region) {
                error_report(
                    "IGVM: Failed to allocate guest memory region for parameters");
                exit(EXIT_FAILURE);
            }
            memcpy(region, param_entry->data, param_entry->size);
            g_free(param_entry->data);
            param_entry->data = NULL;

            result = cgs->set_guest_state(param->gpa, region,
                                                param_entry->size,
                                                CGS_PAGE_TYPE_UNMEASURED, 0);
            if (result != 0) {
                error_report(
                    "IGVM: Failed to set guest state: error_code=%d",
                    result);
                exit(EXIT_FAILURE);
            }
            break;
        }
    }
}

static int cmp_mm_entry(const void *a, const void *b)
{
    const IGVM_VHS_MEMORY_MAP_ENTRY *entry_a =
                        (const IGVM_VHS_MEMORY_MAP_ENTRY *)a;
    const IGVM_VHS_MEMORY_MAP_ENTRY *entry_b =
                        (const IGVM_VHS_MEMORY_MAP_ENTRY *)b;
    if (entry_a->starting_gpa_page_number < entry_b->starting_gpa_page_number) {
        return -1;
    } else if (entry_a->starting_gpa_page_number >
               entry_b->starting_gpa_page_number) {
        return 1;
    } else {
        return 0;
    }
}

static void directive_memory_map(ConfidentialGuestSupport *cgs, int i,
                                 uint32_t compatibility_mask)
{
    IgvmHandle header_handle;
    IGVM_VHS_PARAMETER *param;
    IgvmParameterData *param_entry;
    int max_entry_count;
    int entry = 0;
    IGVM_VHS_MEMORY_MAP_ENTRY *mm_entry;
    ConfidentialGuestMemoryMapEntry cgmm_entry;

    header_handle = igvm_get_header(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
    igvm_handle_error(header_handle,
                      "Failed to read directive header from file");
    param = (IGVM_VHS_PARAMETER *)(igvm_get_buffer(cgs->igvm, header_handle) +
                                   sizeof(IGVM_VHS_VARIABLE_HEADER));

    /* Find the parameter area that should hold the memory map */
    QTAILQ_FOREACH(param_entry, &parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            max_entry_count = param_entry->size /
                              sizeof(IGVM_VHS_MEMORY_MAP_ENTRY);
            mm_entry = (IGVM_VHS_MEMORY_MAP_ENTRY *)param_entry->data;

            while (cgs->get_mem_map_entry(entry, &cgmm_entry) == 0) {
                if (entry > max_entry_count) {
                    error_report(
                        "IGVM: guest memory map size exceeds parameter area defined in IGVM file");
                    exit(EXIT_FAILURE);
                }
                mm_entry[entry].starting_gpa_page_number = cgmm_entry.gpa >> 12;
                mm_entry[entry].number_of_pages = cgmm_entry.size >> 12;

                switch (cgmm_entry.type) {
                case CGS_MEM_RAM:
                    mm_entry[entry].entry_type = MEMORY;
                    break;
                case CGS_MEM_RESERVED:
                    mm_entry[entry].entry_type = PLATFORM_RESERVED;
                    break;
                case CGS_MEM_ACPI:
                    mm_entry[entry].entry_type = PLATFORM_RESERVED;
                    break;
                case CGS_MEM_NVS:
                    mm_entry[entry].entry_type = PERSISTENT;
                    break;
                case CGS_MEM_UNUSABLE:
                    mm_entry[entry].entry_type = PLATFORM_RESERVED;
                    break;
                }
                ++entry;
            }
            /* The entries need to be sorted */
            qsort(mm_entry, entry, sizeof(IGVM_VHS_MEMORY_MAP_ENTRY),
                  cmp_mm_entry);

            break;
        }
    }

    igvm_free_buffer(cgs->igvm, header_handle);
}

static void directive_vp_count(ConfidentialGuestSupport *cgs, int i,
                               uint32_t compatibility_mask)
{
    IgvmHandle header_handle;
    IGVM_VHS_PARAMETER *param;
    IgvmParameterData *param_entry;
    uint32_t *vp_count;
    CPUState *cpu;

    header_handle = igvm_get_header(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
    igvm_handle_error(header_handle,
                      "Failed to read directive header from file");
    param = (IGVM_VHS_PARAMETER *)(igvm_get_buffer(cgs->igvm, header_handle) +
                                   sizeof(IGVM_VHS_VARIABLE_HEADER));

    QTAILQ_FOREACH(param_entry, &parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            vp_count = (uint32_t *)(param_entry->data + param->byte_offset);
            *vp_count = 0;
            CPU_FOREACH(cpu)
            {
                (*vp_count)++;
            }
            break;
        }
    }

    igvm_free_buffer(cgs->igvm, header_handle);
}

static void directive_environment_info(ConfidentialGuestSupport *cgs, int i,
                                       uint32_t compatibility_mask)
{
    IgvmHandle header_handle;
    IGVM_VHS_PARAMETER *param;
    IgvmParameterData *param_entry;
    IgvmEnvironmentInfo *environmental_state;

    header_handle = igvm_get_header(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
    igvm_handle_error(header_handle,
                      "Failed to read directive header from file");
    param = (IGVM_VHS_PARAMETER *)(igvm_get_buffer(cgs->igvm, header_handle) +
                                   sizeof(IGVM_VHS_VARIABLE_HEADER));

    QTAILQ_FOREACH(param_entry, &parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            environmental_state =
                (IgvmEnvironmentInfo *)(param_entry->data + param->byte_offset);
            environmental_state->memory_is_shared = 1;
            break;
        }
    }

    igvm_free_buffer(cgs->igvm, header_handle);
}

static void directive_required_memory(ConfidentialGuestSupport *cgs, int i,
                                      uint32_t compatibility_mask)
{
    IgvmHandle header_handle;
    const IGVM_VHS_REQUIRED_MEMORY *mem;
    uint8_t *region;
    int result;

    header_handle = igvm_get_header(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
    igvm_handle_error(header_handle,
                      "Failed to read directive header from file");
    mem =
        (IGVM_VHS_REQUIRED_MEMORY *)(igvm_get_buffer(cgs->igvm, header_handle) +
                                     sizeof(IGVM_VHS_VARIABLE_HEADER));

    if (mem->compatibility_mask == compatibility_mask) {
        region = igvm_prepare_memory(mem->gpa, mem->number_of_bytes, i);
        result = cgs->set_guest_state(mem->gpa, region,
                                            mem->number_of_bytes,
                                            CGS_PAGE_TYPE_REQUIRED_MEMORY, 0);
        if (result != 0) {
            error_report("IGVM: Failed to set guest state: error_code=%d",
                         result);
            exit(EXIT_FAILURE);
        }
    }
    igvm_free_buffer(cgs->igvm, header_handle);
}

static uint32_t supported_platform_compat_mask(ConfidentialGuestSupport *cgs)
{
    int32_t result;
    int i;
    IgvmHandle header_handle;
    IGVM_VHS_SUPPORTED_PLATFORM *platform;
    uint32_t compatibility_mask = 0;

    result = igvm_header_count(cgs->igvm, HEADER_SECTION_PLATFORM);
    igvm_handle_error(result, "Failed to read platform header count");

    for (i = 0; i < (int)result; ++i) {
        IgvmVariableHeaderType typ =
            igvm_get_header_type(cgs->igvm, HEADER_SECTION_PLATFORM, i);
        if (typ == IGVM_VHT_SUPPORTED_PLATFORM) {
            header_handle =
                igvm_get_header(cgs->igvm, HEADER_SECTION_PLATFORM, i);
            igvm_handle_error(header_handle,
                              "Failed to read platform header from file");
            platform =
                (IGVM_VHS_SUPPORTED_PLATFORM *)(igvm_get_buffer(cgs->igvm,
                                                                header_handle) +
                                                sizeof(
                                                    IGVM_VHS_VARIABLE_HEADER));
            /* Currently only support SEV-SNP. */
            if (platform->platform_type == SEV_SNP) {
                /*
                 * IGVM does not define a platform types of SEV or SEV_ES.
                 * Translate SEV_SNP into CGS_PLATFORM_SEV_ES and
                 * CGS_PLATFORM_SEV and let the cgs function implementations
                 * check whether each IGVM directive results in an operation
                 * that is supported by the particular derivative of SEV.
                 */
                if (cgs->check_support(
                        CGS_PLATFORM_SEV_SNP, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary) ||
                    cgs->check_support(
                        CGS_PLATFORM_SEV_ES, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary) ||
                    cgs->check_support(
                        CGS_PLATFORM_SEV, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary)) {
                    compatibility_mask = platform->compatibility_mask;
                    break;
                }
            } else if (platform->platform_type == TDX) {
                if (cgs->check_support(
                        CGS_PLATFORM_TDX, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary)) {
                    compatibility_mask = platform->compatibility_mask;
                    break;
                }
            }
            igvm_free_buffer(cgs->igvm, header_handle);
        }
    }
    return compatibility_mask;
}

void igvm_file_init(ConfidentialGuestSupport *cgs)
{
    FILE *igvm_file = NULL;
    uint8_t *igvm_buf = NULL;

    if (cgs->igvm_filename) {
        IgvmHandle igvm;
        unsigned long igvm_length;

        igvm_file = fopen(cgs->igvm_filename, "rb");
        if (!igvm_file) {
            error_report("IGVM file not found '%s'", cgs->igvm_filename);
            goto error_out;
        }

        fseek(igvm_file, 0, SEEK_END);
        igvm_length = ftell(igvm_file);
        fseek(igvm_file, 0, SEEK_SET);

        igvm_buf = g_new(uint8_t, igvm_length);
        if (!igvm_buf) {
            error_report(
                "Could not allocate buffer to read file IGVM file '%s'",
                cgs->igvm_filename);
            goto error_out;
        }
        if (fread(igvm_buf, 1, igvm_length, igvm_file) != igvm_length) {
            error_report("Unable to load IGVM file '%s'", cgs->igvm_filename);
            goto error_out;
        }

        igvm = igvm_new_from_binary(igvm_buf, igvm_length);
        if (igvm < 0) {
            error_report("Parsing IGVM file '%s' failed with  error_code %d",
                         cgs->igvm_filename, igvm);
            goto error_out;
        }
        fclose(igvm_file);
        g_free(igvm_buf);

        cgs->igvm = igvm;
    }
    return;

error_out:
    free(igvm_buf);
    if (igvm_file) {
        fclose(igvm_file);
    }
    exit(EXIT_FAILURE);
}

void igvm_process(ConfidentialGuestSupport *cgs)
{
    int32_t result;
    int i;
    uint32_t compatibility_mask;
    IgvmParameterData *parameter;

    /*
     * If this is not a Confidential guest or no IGVM has been provided then
     * this is a no-op.
     */
    if (!cgs || !cgs->igvm) {
        return;
    }

    QTAILQ_INIT(&parameter_data);

    /*
     * Check that the IGVM file provides configuration for the current
     * platform
     */
    compatibility_mask = supported_platform_compat_mask(cgs);
    if (compatibility_mask == 0) {
        error_report(
            "IGVM file does not describe a compatible supported platform");
        exit(EXIT_FAILURE);
    }

    result = igvm_header_count(cgs->igvm, HEADER_SECTION_DIRECTIVE);
    igvm_handle_error(result, "Failed to read directive header count");
    for (i = 0; i < (int)result; ++i) {
        IgvmVariableHeaderType type =
            igvm_get_header_type(cgs->igvm, HEADER_SECTION_DIRECTIVE, i);
        directive(type, cgs, i, compatibility_mask);
    }

    /*
     * Contiguous pages of data with compatible flags are grouped together in
     * order to reduce the number of memory regions we create. Make sure the
     * last group is processed with this call.
     */
    process_mem_page(cgs, i, NULL);

    QTAILQ_FOREACH(parameter, &parameter_data, next)
    {
        g_free(parameter->data);
        parameter->data = NULL;
    }
}

#endif
