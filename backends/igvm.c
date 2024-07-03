/*
 * QEMU IGVM configuration backend for guests
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

#include "igvm.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/core/cpu.h"

#include <igvm/igvm.h>
#include <igvm/igvm_defs.h>

typedef struct IgvmParameterData {
    QTAILQ_ENTRY(IgvmParameterData) next;
    uint8_t *data;
    uint32_t size;
    uint32_t index;
} IgvmParameterData;

/*
 * QemuIgvm contains the information required during processing
 * of a single IGVM file.
 */
typedef struct QemuIgvm {
    IgvmHandle file;
    ConfidentialGuestSupport *cgs;
    ConfidentialGuestSupportClass *cgsc;
    uint32_t compatibility_mask;
    unsigned current_header_index;
    QTAILQ_HEAD(, IgvmParameterData) parameter_data;

    /* These variables keep track of contiguous page regions */
    IGVM_VHS_PAGE_DATA region_prev_page_data;
    uint64_t region_start;
    unsigned region_start_index;
    unsigned region_last_index;
    unsigned region_page_count;
} QemuIgvm;

static int directive_page_data(QemuIgvm *ctx, const uint8_t *header_data,
                               Error **errp);
static int directive_vp_context(QemuIgvm *ctx, const uint8_t *header_data,
                                Error **errp);
static int directive_parameter_area(QemuIgvm *ctx, const uint8_t *header_data,
                                    Error **errp);
static int directive_parameter_insert(QemuIgvm *ctx, const uint8_t *header_data,
                                      Error **errp);
static int directive_memory_map(QemuIgvm *ctx, const uint8_t *header_data,
                                Error **errp);
static int directive_vp_count(QemuIgvm *ctx, const uint8_t *header_data,
                              Error **errp);
static int directive_environment_info(QemuIgvm *ctx, const uint8_t *header_data,
                                      Error **errp);
static int directive_required_memory(QemuIgvm *ctx, const uint8_t *header_data,
                                     Error **errp);

struct IGVMHandler {
    uint32_t type;
    uint32_t section;
    int (*handler)(QemuIgvm *ctx, const uint8_t *header_data, Error **errp);
};

static struct IGVMHandler handlers[] = {
    { IGVM_VHT_PAGE_DATA, IGVM_HEADER_SECTION_DIRECTIVE, directive_page_data },
    { IGVM_VHT_VP_CONTEXT, IGVM_HEADER_SECTION_DIRECTIVE,
      directive_vp_context },
    { IGVM_VHT_PARAMETER_AREA, IGVM_HEADER_SECTION_DIRECTIVE,
      directive_parameter_area },
    { IGVM_VHT_PARAMETER_INSERT, IGVM_HEADER_SECTION_DIRECTIVE,
      directive_parameter_insert },
    { IGVM_VHT_MEMORY_MAP, IGVM_HEADER_SECTION_DIRECTIVE,
      directive_memory_map },
    { IGVM_VHT_VP_COUNT_PARAMETER, IGVM_HEADER_SECTION_DIRECTIVE,
      directive_vp_count },
    { IGVM_VHT_ENVIRONMENT_INFO_PARAMETER, IGVM_HEADER_SECTION_DIRECTIVE,
      directive_environment_info },
    { IGVM_VHT_REQUIRED_MEMORY, IGVM_HEADER_SECTION_DIRECTIVE,
      directive_required_memory },
};

static int handler(QemuIgvm *ctx, uint32_t type, Error **errp)
{
    size_t handler;
    IgvmHandle header_handle;
    const uint8_t *header_data;
    int result;

    for (handler = 0; handler < G_N_ELEMENTS(handlers); handler++) {
        if (handlers[handler].type != type) {
            continue;
        }
        header_handle = igvm_get_header(ctx->file,
                                        handlers[handler].section,
                                        ctx->current_header_index);
        if (header_handle < 0) {
            error_setg(
                errp,
                "IGVM file is invalid: Failed to read directive header (code: %d)",
                (int)header_handle);
            return -1;
        }
        header_data = igvm_get_buffer(ctx->file, header_handle) +
                      sizeof(IGVM_VHS_VARIABLE_HEADER);
        result = handlers[handler].handler(ctx, header_data, errp);
        igvm_free_buffer(ctx->file, header_handle);
        return result;
    }
    error_setg(errp,
               "IGVM: Unknown header type encountered when processing file: "
               "(type 0x%X)",
               type);
    return -1;
}

static void *igvm_prepare_memory(QemuIgvm *ctx, uint64_t addr, uint64_t size,
                                 int region_identifier, Error **errp)
{
    ERRP_GUARD();
    MemoryRegion *igvm_pages = NULL;
    Int128 gpa_region_size;
    MemoryRegionSection mrs =
        memory_region_find(get_system_memory(), addr, size);
    if (mrs.mr) {
        if (!memory_region_is_ram(mrs.mr)) {
            memory_region_unref(mrs.mr);
            error_setg(
                errp,
                "Processing of IGVM file failed: Could not prepare memory "
                "at address 0x%lX due to existing non-RAM region",
                addr);
            return NULL;
        }

        gpa_region_size = int128_make64(size);
        if (int128_lt(mrs.size, gpa_region_size)) {
            memory_region_unref(mrs.mr);
            error_setg(
                errp,
                "Processing of IGVM file failed: Could not prepare memory "
                "at address 0x%lX: region size exceeded",
                addr);
            return NULL;
        }
        return qemu_map_ram_ptr(mrs.mr->ram_block, mrs.offset_within_region);
    } else {
        /*
         * The region_identifier is the is the index of the IGVM directive that
         * contains the page with the lowest GPA in the region. This will
         * generate a unique region name.
         */
        g_autofree char *region_name =
            g_strdup_printf("igvm.%X", region_identifier);
        igvm_pages = g_malloc(sizeof(*igvm_pages));
        if (ctx->cgs && ctx->cgs->require_guest_memfd) {
            if (!memory_region_init_ram_guest_memfd(igvm_pages, NULL,
                                                    region_name, size, errp)) {
                return NULL;
            }
        } else {
            if (!memory_region_init_ram(igvm_pages, NULL, region_name, size,
                                        errp)) {
                return NULL;
            }
        }
        memory_region_add_subregion(get_system_memory(), addr, igvm_pages);
        return memory_region_get_ram_ptr(igvm_pages);
    }
}

static int igvm_type_to_cgs_type(IgvmPageDataType memory_type, bool unmeasured,
                                 bool zero)
{
    switch (memory_type) {
    case IGVM_PAGE_DATA_TYPE_NORMAL: {
        if (unmeasured) {
            return CGS_PAGE_TYPE_UNMEASURED;
        } else {
            return zero ? CGS_PAGE_TYPE_ZERO : CGS_PAGE_TYPE_NORMAL;
        }
    }
    case IGVM_PAGE_DATA_TYPE_SECRETS:
        return CGS_PAGE_TYPE_SECRETS;
    case IGVM_PAGE_DATA_TYPE_CPUID_DATA:
        return CGS_PAGE_TYPE_CPUID;
    case IGVM_PAGE_DATA_TYPE_CPUID_XF:
        return CGS_PAGE_TYPE_CPUID;
    default:
        return -1;
    }
}

static bool page_attrs_equal(IgvmHandle igvm, unsigned header_index,
                             const IGVM_VHS_PAGE_DATA *page_1,
                             const IGVM_VHS_PAGE_DATA *page_2)
{
    IgvmHandle data_handle1, data_handle2;

    /*
     * If one page has data and the other doesn't then this results in different
     * page types: NORMAL vs ZERO.
     */
    data_handle1 = igvm_get_header_data(igvm, IGVM_HEADER_SECTION_DIRECTIVE,
                                        header_index - 1);
    data_handle2 =
        igvm_get_header_data(igvm, IGVM_HEADER_SECTION_DIRECTIVE, header_index);
    if ((data_handle1 == IGVMAPI_NO_DATA) &&
        (data_handle2 != IGVMAPI_NO_DATA)) {
        return false;
    } else if ((data_handle1 != IGVMAPI_NO_DATA) &&
               (data_handle2 == IGVMAPI_NO_DATA)) {
        return false;
    }
    return ((*(const uint32_t *)&page_1->flags ==
             *(const uint32_t *)&page_2->flags) &&
            (page_1->data_type == page_2->data_type) &&
            (page_1->compatibility_mask == page_2->compatibility_mask));
}

static int igvm_process_mem_region(QemuIgvm *ctx, unsigned start_index,
                                   uint64_t gpa_start, unsigned page_count,
                                   const IgvmPageDataFlags *flags,
                                   const IgvmPageDataType page_type,
                                   Error **errp)
{
    uint8_t *region;
    IgvmHandle data_handle;
    const void *data;
    uint32_t data_size;
    unsigned page_index;
    bool zero = true;
    const uint64_t page_size = flags->is_2mb_page ? 0x200000 : 0x1000;
    int result;
    int cgs_page_type;

    region = igvm_prepare_memory(ctx, gpa_start, page_count * page_size,
                                 start_index, errp);
    if (!region) {
        return -1;
    }

    for (page_index = 0; page_index < page_count; page_index++) {
        data_handle = igvm_get_header_data(
            ctx->file, IGVM_HEADER_SECTION_DIRECTIVE, page_index + start_index);
        if (data_handle == IGVMAPI_NO_DATA) {
            /* No data indicates a zero page */
            memset(&region[page_index * page_size], 0, page_size);
        } else if (data_handle < 0) {
            error_setg(
                errp,
                "IGVM file contains invalid page data for directive with "
                "index %d",
                page_index + start_index);
            return -1;
        } else {
            zero = false;
            data_size = igvm_get_buffer_size(ctx->file, data_handle);
            if (data_size < page_size) {
                memset(&region[page_index * page_size], 0, page_size);
            } else if (data_size > page_size) {
                error_setg(errp,
                           "IGVM file contains page data with invalid size for "
                           "directive with index %d",
                           page_index + start_index);
                return -1;
            }
            data = igvm_get_buffer(ctx->file, data_handle);
            memcpy(&region[page_index * page_size], data, data_size);
            igvm_free_buffer(ctx->file, data_handle);
        }
    }

    /*
     * If a confidential guest support object is provided then use it to set the
     * guest state.
     */
    if (ctx->cgs) {
        cgs_page_type =
            igvm_type_to_cgs_type(page_type, flags->unmeasured, zero);
        if (cgs_page_type < 0) {
            error_setg(errp,
                       "Invalid page type in IGVM file. Directives: %d to %d, "
                       "page type: %d",
                       start_index, start_index + page_count, page_type);
            return -1;
        }

        result = ctx->cgsc->set_guest_state(
            gpa_start, region, page_size * page_count, cgs_page_type, 0, errp);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

static int process_mem_page(QemuIgvm *ctx, const IGVM_VHS_PAGE_DATA *page_data,
                            Error **errp)
{
    if (page_data) {
        if (ctx->region_page_count == 0) {
            ctx->region_start = page_data->gpa;
            ctx->region_start_index = ctx->current_header_index;
        } else {
            if (!page_attrs_equal(ctx->file, ctx->current_header_index,
                                  page_data, &ctx->region_prev_page_data) ||
                ((ctx->region_prev_page_data.gpa +
                  (ctx->region_prev_page_data.flags.is_2mb_page ? 0x200000 :
                                                                  0x1000)) !=
                 page_data->gpa) ||
                (ctx->region_last_index != (ctx->current_header_index - 1))) {
                /* End of current region */
                if (igvm_process_mem_region(
                        ctx, ctx->region_start_index, ctx->region_start,
                        ctx->region_page_count,
                        &ctx->region_prev_page_data.flags,
                        ctx->region_prev_page_data.data_type, errp) < 0) {
                    return -1;
                }
                ctx->region_page_count = 0;
                ctx->region_start = page_data->gpa;
                ctx->region_start_index = ctx->current_header_index;
            }
        }
        memcpy(&ctx->region_prev_page_data, page_data,
               sizeof(ctx->region_prev_page_data));
        ctx->region_last_index = ctx->current_header_index;
        ctx->region_page_count++;
    } else {
        if (ctx->region_page_count > 0) {
            if (igvm_process_mem_region(
                    ctx, ctx->region_start_index, ctx->region_start,
                    ctx->region_page_count, &ctx->region_prev_page_data.flags,
                    ctx->region_prev_page_data.data_type, errp) < 0) {
                return -1;
            }
            ctx->region_page_count = 0;
        }
    }
    return 0;
}

static int directive_page_data(QemuIgvm *ctx, const uint8_t *header_data,
                               Error **errp)
{
    const IGVM_VHS_PAGE_DATA *page_data =
        (const IGVM_VHS_PAGE_DATA *)header_data;
    if (page_data->compatibility_mask & ctx->compatibility_mask) {
        return process_mem_page(ctx, page_data, errp);
    }
    return 0;
}

static int directive_vp_context(QemuIgvm *ctx, const uint8_t *header_data,
                                Error **errp)
{
    const IGVM_VHS_VP_CONTEXT *vp_context =
        (const IGVM_VHS_VP_CONTEXT *)header_data;
    IgvmHandle data_handle;
    uint8_t *data;
    int result;

    if (!(vp_context->compatibility_mask & ctx->compatibility_mask)) {
        return 0;
    }

    /*
     * A confidential guest support object must be provided for setting
     * a VP context.
     */
    if (!ctx->cgs) {
        error_setg(
            errp,
            "A VP context is present in the IGVM file but is not supported "
            "by the current system.");
        return -1;
    }

    data_handle = igvm_get_header_data(ctx->file,
                                        IGVM_HEADER_SECTION_DIRECTIVE,
                                        ctx->current_header_index);
    if (data_handle < 0) {
        error_setg(errp, "Invalid VP context in IGVM file. Error code: %X",
                    data_handle);
        return -1;
    }

    data = (uint8_t *)igvm_get_buffer(ctx->file, data_handle);
    result = ctx->cgsc->set_guest_state(
        vp_context->gpa, data, igvm_get_buffer_size(ctx->file, data_handle),
        CGS_PAGE_TYPE_VMSA, vp_context->vp_index, errp);
    igvm_free_buffer(ctx->file, data_handle);
    if (result < 0) {
        return result;
    }
    return 0;
}

static int directive_parameter_area(QemuIgvm *ctx, const uint8_t *header_data,
                                    Error **errp)
{
    const IGVM_VHS_PARAMETER_AREA *param_area =
        (const IGVM_VHS_PARAMETER_AREA *)header_data;
    IgvmParameterData *param_entry;

    param_entry = g_new0(IgvmParameterData, 1);
    param_entry->size = param_area->number_of_bytes;
    param_entry->index = param_area->parameter_area_index;
    param_entry->data = g_malloc0(param_entry->size);

    QTAILQ_INSERT_TAIL(&ctx->parameter_data, param_entry, next);
    return 0;
}

static int directive_parameter_insert(QemuIgvm *ctx, const uint8_t *header_data,
                                      Error **errp)
{
    const IGVM_VHS_PARAMETER_INSERT *param =
        (const IGVM_VHS_PARAMETER_INSERT *)header_data;
    IgvmParameterData *param_entry;
    int result;
    void *region;

    if (!(param->compatibility_mask & ctx->compatibility_mask)) {
        return 0;
    }

    QTAILQ_FOREACH(param_entry, &ctx->parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            region = igvm_prepare_memory(ctx, param->gpa, param_entry->size,
                                         ctx->current_header_index, errp);
            if (!region) {
                return -1;
            }
            memcpy(region, param_entry->data, param_entry->size);
            g_free(param_entry->data);
            param_entry->data = NULL;

            /*
             * If a confidential guest support object is provided then use it to
             * set the guest state.
             */
            if (ctx->cgs) {
                result = ctx->cgsc->set_guest_state(param->gpa, region,
                                                    param_entry->size,
                                                    CGS_PAGE_TYPE_UNMEASURED, 0,
                                                    errp);
                if (result < 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
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

static int directive_memory_map(QemuIgvm *ctx, const uint8_t *header_data,
                                Error **errp)
{
    const IGVM_VHS_PARAMETER *param = (const IGVM_VHS_PARAMETER *)header_data;
    IgvmParameterData *param_entry;
    int max_entry_count;
    int entry = 0;
    IGVM_VHS_MEMORY_MAP_ENTRY *mm_entry;
    ConfidentialGuestMemoryMapEntry cgmm_entry;
    int retval = 0;

    if (!ctx->cgs) {
        error_setg(errp,
                   "IGVM file contains a memory map but this is not supported "
                   "by the current system.");
        return -1;
    }

    /* Find the parameter area that should hold the memory map */
    QTAILQ_FOREACH(param_entry, &ctx->parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            max_entry_count =
                param_entry->size / sizeof(IGVM_VHS_MEMORY_MAP_ENTRY);
            mm_entry = (IGVM_VHS_MEMORY_MAP_ENTRY *)param_entry->data;

            retval = ctx->cgsc->get_mem_map_entry(entry, &cgmm_entry, errp);
            while (retval == 0) {
                if (entry > max_entry_count) {
                    error_setg(
                        errp,
                        "IGVM: guest memory map size exceeds parameter area defined in IGVM file");
                    return -1;
                }
                mm_entry[entry].starting_gpa_page_number = cgmm_entry.gpa >> 12;
                mm_entry[entry].number_of_pages = cgmm_entry.size >> 12;

                switch (cgmm_entry.type) {
                case CGS_MEM_RAM:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_MEMORY;
                    break;
                case CGS_MEM_RESERVED:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_PLATFORM_RESERVED;
                    break;
                case CGS_MEM_ACPI:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_PLATFORM_RESERVED;
                    break;
                case CGS_MEM_NVS:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_PERSISTENT;
                    break;
                case CGS_MEM_UNUSABLE:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_PLATFORM_RESERVED;
                    break;
                }
                retval =
                    ctx->cgsc->get_mem_map_entry(++entry, &cgmm_entry, errp);
            }
            if (retval < 0) {
                return retval;
            }
            /* The entries need to be sorted */
            qsort(mm_entry, entry, sizeof(IGVM_VHS_MEMORY_MAP_ENTRY),
                  cmp_mm_entry);

            break;
        }
    }
    return 0;
}

static int directive_vp_count(QemuIgvm *ctx, const uint8_t *header_data,
                              Error **errp)
{
    const IGVM_VHS_PARAMETER *param = (const IGVM_VHS_PARAMETER *)header_data;
    IgvmParameterData *param_entry;
    uint32_t *vp_count;
    CPUState *cpu;

    QTAILQ_FOREACH(param_entry, &ctx->parameter_data, next)
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
    return 0;
}

static int directive_environment_info(QemuIgvm *ctx, const uint8_t *header_data,
                                      Error **errp)
{
    const IGVM_VHS_PARAMETER *param = (const IGVM_VHS_PARAMETER *)header_data;
    IgvmParameterData *param_entry;
    IgvmEnvironmentInfo *environmental_state;

    QTAILQ_FOREACH(param_entry, &ctx->parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            environmental_state =
                (IgvmEnvironmentInfo *)(param_entry->data + param->byte_offset);
            environmental_state->memory_is_shared = 1;
            break;
        }
    }
    return 0;
}

static int directive_required_memory(QemuIgvm *ctx, const uint8_t *header_data,
                                     Error **errp)
{
    const IGVM_VHS_REQUIRED_MEMORY *mem =
        (const IGVM_VHS_REQUIRED_MEMORY *)header_data;
    uint8_t *region;
    int result;

    if (!(mem->compatibility_mask & ctx->compatibility_mask)) {
        return 0;
    }

    region = igvm_prepare_memory(ctx, mem->gpa, mem->number_of_bytes,
                                    ctx->current_header_index, errp);
    if (!region) {
        return -1;
    }
    if (ctx->cgs) {
        result = ctx->cgsc->set_guest_state(mem->gpa, region,
                                            mem->number_of_bytes,
                                            CGS_PAGE_TYPE_REQUIRED_MEMORY,
                                            0, errp);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

static int supported_platform_compat_mask(QemuIgvm *ctx, Error **errp)
{
    int32_t header_count;
    unsigned header_index;
    IgvmHandle header_handle;
    IGVM_VHS_SUPPORTED_PLATFORM *platform;
    uint32_t compatibility_mask_sev = 0;
    uint32_t compatibility_mask_sev_es = 0;
    uint32_t compatibility_mask_sev_snp = 0;
    uint32_t compatibility_mask = 0;

    header_count = igvm_header_count(ctx->file, IGVM_HEADER_SECTION_PLATFORM);
    if (header_count < 0) {
        error_setg(errp,
                   "Invalid platform header count in IGVM file. Error code: %X",
                   header_count);
        return -1;
    }

    for (header_index = 0; header_index < (unsigned)header_count;
         header_index++) {
        IgvmVariableHeaderType typ = igvm_get_header_type(
            ctx->file, IGVM_HEADER_SECTION_PLATFORM, header_index);
        if (typ == IGVM_VHT_SUPPORTED_PLATFORM) {
            header_handle = igvm_get_header(
                ctx->file, IGVM_HEADER_SECTION_PLATFORM, header_index);
            if (header_handle < 0) {
                error_setg(errp,
                           "Invalid platform header in IGVM file. "
                           "Index: %d, Error code: %X",
                           header_index, header_handle);
                return -1;
            }
            platform =
                (IGVM_VHS_SUPPORTED_PLATFORM *)(igvm_get_buffer(ctx->file,
                                                                header_handle) +
                                                sizeof(
                                                    IGVM_VHS_VARIABLE_HEADER));
            if ((platform->platform_type == IGVM_PLATFORM_TYPE_SEV_ES) &&
                ctx->cgs) {
                if (ctx->cgsc->check_support(
                        CGS_PLATFORM_SEV_ES, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary)) {
                    compatibility_mask_sev_es = platform->compatibility_mask;
                }
            } else if ((platform->platform_type == IGVM_PLATFORM_TYPE_SEV) &&
                ctx->cgs) {
                if (ctx->cgsc->check_support(
                        CGS_PLATFORM_SEV, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary)) {
                    compatibility_mask_sev = platform->compatibility_mask;
                }
            } else if ((platform->platform_type ==
                        IGVM_PLATFORM_TYPE_SEV_SNP) &&
                       ctx->cgs) {
                if (ctx->cgsc->check_support(
                        CGS_PLATFORM_SEV_SNP, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary)) {
                    compatibility_mask_sev_snp = platform->compatibility_mask;
                }
            } else if (platform->platform_type == IGVM_PLATFORM_TYPE_NATIVE) {
                compatibility_mask = platform->compatibility_mask;
            }
            igvm_free_buffer(ctx->file, header_handle);
        }
    }
    /* Choose the strongest supported isolation technology */
    if (compatibility_mask_sev_snp != 0) {
        ctx->compatibility_mask = compatibility_mask_sev_snp;
    } else if (compatibility_mask_sev_es != 0) {
        ctx->compatibility_mask = compatibility_mask_sev_es;
    } else if (compatibility_mask_sev != 0) {
        ctx->compatibility_mask = compatibility_mask_sev;
    } else if (compatibility_mask != 0) {
        ctx->compatibility_mask = compatibility_mask;
    } else {
        error_setg(
            errp,
            "IGVM file does not describe a compatible supported platform");
        return -1;
    }
    return 0;
}

static IgvmHandle igvm_file_init(char *filename, Error **errp)
{
    IgvmHandle igvm;
    g_autofree uint8_t *buf = NULL;
    unsigned long len;
    g_autoptr(GError) gerr = NULL;

    if (!g_file_get_contents(filename, (gchar **)&buf, &len, &gerr)) {
        error_setg(errp, "Unable to load %s: %s", filename, gerr->message);
        return -1;
    }

    igvm = igvm_new_from_binary(buf, len);
    if (igvm < 0) {
        error_setg(errp, "Unable to parse IGVM file %s: %d", filename, igvm);
        return -1;
    }
    return igvm;
}

int igvm_process_file(IgvmCfgState *cfg, ConfidentialGuestSupport *cgs,
                      Error **errp)
{
    int32_t header_count;
    IgvmParameterData *parameter;
    int retval = -1;
    QemuIgvm ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.file = igvm_file_init(cfg->filename, errp);
    if (ctx.file < 0) {
        return -1;
    }

    /*
     * The ConfidentialGuestSupport object is optional and allows a confidential
     * guest platform to perform extra processing, such as page measurement, on
     * IGVM directives.
     */
    ctx.cgs = cgs;
    ctx.cgsc = cgs ? CONFIDENTIAL_GUEST_SUPPORT_GET_CLASS(cgs) : NULL;

    /*
     * Check that the IGVM file provides configuration for the current
     * platform
     */
    if (supported_platform_compat_mask(&ctx, errp) < 0) {
        return -1;
    }

    header_count = igvm_header_count(ctx.file, IGVM_HEADER_SECTION_DIRECTIVE);
    if (header_count <= 0) {
        error_setg(
            errp, "Invalid directive header count in IGVM file. Error code: %X",
            header_count);
        return -1;
    }

    QTAILQ_INIT(&ctx.parameter_data);

    for (ctx.current_header_index = 0;
         ctx.current_header_index < (unsigned)header_count;
         ctx.current_header_index++) {
        IgvmVariableHeaderType type = igvm_get_header_type(
            ctx.file, IGVM_HEADER_SECTION_DIRECTIVE, ctx.current_header_index);
        if (handler(&ctx, type, errp) < 0) {
            goto cleanup;
        }
    }

    /*
     * Contiguous pages of data with compatible flags are grouped together in
     * order to reduce the number of memory regions we create. Make sure the
     * last group is processed with this call.
     */
    retval = process_mem_page(&ctx, NULL, errp);

cleanup:
    QTAILQ_FOREACH(parameter, &ctx.parameter_data, next)
    {
        g_free(parameter->data);
        parameter->data = NULL;
    }

    return retval;
}
