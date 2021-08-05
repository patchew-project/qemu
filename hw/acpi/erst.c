/*
 * ACPI Error Record Serialization Table, ERST, Implementation
 *
 * ACPI ERST introduced in ACPI 4.0, June 16, 2009.
 * ACPI Platform Error Interfaces : Error Serialization
 *
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-core.h"
#include "exec/memory.h"
#include "qom/object.h"
#include "hw/pci/pci.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/bios-linker-loader.h"
#include "exec/address-spaces.h"
#include "sysemu/hostmem.h"
#include "hw/acpi/erst.h"
#include "trace.h"

/* ACPI 4.0: Table 17-16 Serialization Actions */
#define ACTION_BEGIN_WRITE_OPERATION         0x0
#define ACTION_BEGIN_READ_OPERATION          0x1
#define ACTION_BEGIN_CLEAR_OPERATION         0x2
#define ACTION_END_OPERATION                 0x3
#define ACTION_SET_RECORD_OFFSET             0x4
#define ACTION_EXECUTE_OPERATION             0x5
#define ACTION_CHECK_BUSY_STATUS             0x6
#define ACTION_GET_COMMAND_STATUS            0x7
#define ACTION_GET_RECORD_IDENTIFIER         0x8
#define ACTION_SET_RECORD_IDENTIFIER         0x9
#define ACTION_GET_RECORD_COUNT              0xA
#define ACTION_BEGIN_DUMMY_WRITE_OPERATION   0xB
#define ACTION_RESERVED                      0xC
#define ACTION_GET_ERROR_LOG_ADDRESS_RANGE   0xD
#define ACTION_GET_ERROR_LOG_ADDRESS_LENGTH  0xE
#define ACTION_GET_ERROR_LOG_ADDRESS_RANGE_ATTRIBUTES 0xF
#define ACTION_GET_EXECUTE_OPERATION_TIMINGS 0x10

/* ACPI 4.0: Table 17-17 Command Status Definitions */
#define STATUS_SUCCESS                0x00
#define STATUS_NOT_ENOUGH_SPACE       0x01
#define STATUS_HARDWARE_NOT_AVAILABLE 0x02
#define STATUS_FAILED                 0x03
#define STATUS_RECORD_STORE_EMPTY     0x04
#define STATUS_RECORD_NOT_FOUND       0x05


/* UEFI 2.1: Appendix N Common Platform Error Record */
#define UEFI_CPER_RECORD_MIN_SIZE 128U
#define UEFI_CPER_RECORD_LENGTH_OFFSET 20U
#define UEFI_CPER_RECORD_ID_OFFSET 96U
#define IS_UEFI_CPER_RECORD(ptr) \
    (((ptr)[0] == 'C') && \
     ((ptr)[1] == 'P') && \
     ((ptr)[2] == 'E') && \
     ((ptr)[3] == 'R'))
#define THE_UEFI_CPER_RECORD_ID(ptr) \
    (*(uint64_t *)(&(ptr)[UEFI_CPER_RECORD_ID_OFFSET]))

/*
 * This implementation is an ACTION (cmd) and VALUE (data)
 * interface consisting of just two 64-bit registers.
 */
#define ERST_REG_SIZE (16UL)
#define ERST_ACTION_OFFSET (0UL) /* action (cmd) */
#define ERST_VALUE_OFFSET  (8UL) /* argument/value (data) */

/*
 * ERST_RECORD_SIZE is the buffer size for exchanging ERST
 * record contents. Thus, it defines the maximum record size.
 * As this is mapped through a PCI BAR, it must be a power of
 * two and larger than UEFI_CPER_RECORD_MIN_SIZE.
 * The backing storage is divided into fixed size "slots",
 * each ERST_RECORD_SIZE in length, and each "slot"
 * storing a single record. No attempt at optimizing storage
 * through compression, compaction, etc is attempted.
 * NOTE that slot 0 is reserved for the backing storage header.
 * Depending upon the size of the backing storage, additional
 * slots will be part of the slot 0 header in order to account
 * for a record_id for each available remaining slot.
 */
/* 8KiB records, not too small, not too big */
#define ERST_RECORD_SIZE (8192UL)

#define ACPI_ERST_MEMDEV_PROP "memdev"

/*
 * From the ACPI ERST spec sections:
 * A record id of all 0s is used to indicate
 * 'unspecified' record id.
 * A record id of all 1s is used to indicate
 * empty or end.
 */
#define ERST_UNSPECIFIED_RECORD_ID (0UL)
#define ERST_EMPTY_END_RECORD_ID (~0UL)
#define ERST_EXECUTE_OPERATION_MAGIC 0x9CUL
#define ERST_IS_VALID_RECORD_ID(rid) \
    ((rid != ERST_UNSPECIFIED_RECORD_ID) && \
     (rid != ERST_EMPTY_END_RECORD_ID))

typedef struct erst_storage_header_s {
#define ERST_STORE_MAGIC 0x524F545354535245UL
    uint64_t magic;
    uint32_t record_size;
    uint32_t record_offset; /* offset to record storage beyond header */
    uint16_t version;
    uint16_t reserved;
    uint32_t record_count;
    uint64_t map[]; /* contains record_ids, and position indicates index */
} erst_storage_header_t;

/*
 * Object cast macro
 */
#define ACPIERST(obj) \
    OBJECT_CHECK(ERSTDeviceState, (obj), TYPE_ACPI_ERST)

/*
 * Main ERST device state structure
 */
typedef struct {
    PCIDevice parent_obj;

    /* Backend storage */
    HostMemoryBackend *hostmem;
    MemoryRegion *hostmem_mr;

    /* Programming registers */
    MemoryRegion iomem;

    /* Exchange buffer */
    Object *exchange_obj;
    HostMemoryBackend *exchange;
    MemoryRegion *exchange_mr;
    uint32_t storage_size;

    /* Interface state */
    uint8_t operation;
    uint8_t busy_status;
    uint8_t command_status;
    uint32_t record_offset;
    uint64_t reg_action;
    uint64_t reg_value;
    uint64_t record_identifier;
    erst_storage_header_t *header;
    unsigned next_record_index;
    unsigned first_record_index;
    unsigned last_record_index;

} ERSTDeviceState;

/*******************************************************************/
/*******************************************************************/

static uint8_t *get_nvram_ptr_by_index(ERSTDeviceState *s, unsigned index)
{
    uint8_t *rc = NULL;
    off_t offset = (index * ERST_RECORD_SIZE);
    if ((offset + ERST_RECORD_SIZE) <= s->storage_size) {
        if (s->hostmem_mr) {
            uint8_t *p = (uint8_t *)memory_region_get_ram_ptr(s->hostmem_mr);
            rc = p + offset;
        }
    }
    return rc;
}

static void make_erst_storage_header(ERSTDeviceState *s)
{
    erst_storage_header_t *header = s->header;
    unsigned mapsz, headersz;

    header->magic = ERST_STORE_MAGIC;
    header->record_size = ERST_RECORD_SIZE;
    header->version = 0x0101;
    header->reserved = 0x0000;

    /* Compute mapsize */
    mapsz = s->storage_size / ERST_RECORD_SIZE;
    mapsz *= sizeof(uint64_t);
    /* Compute header+map size */
    headersz = sizeof(erst_storage_header_t) + mapsz;
    /* Round up to nearest integer multiple of ERST_RECORD_SIZE */
    headersz += (ERST_RECORD_SIZE - 1);
    headersz /= ERST_RECORD_SIZE;
    headersz *= ERST_RECORD_SIZE;
    header->record_offset = headersz;

    /*
     * The HostMemoryBackend initializes contents to zero,
     * so all record_ids stashed in the map are zero'd.
     * As well the record_count is zero. Properly initialized.
     */
}

static void check_erst_backend_storage(ERSTDeviceState *s, Error **errp)
{
    erst_storage_header_t *header;

    header = (erst_storage_header_t *)get_nvram_ptr_by_index(s, 0);
    s->header = header;

    /* Check if header is uninitialized */
    if (header->magic == 0UL) { /* HostMemoryBackend inits to 0 */
        make_erst_storage_header(s);
    }

    if (!(
        (header->magic == ERST_STORE_MAGIC) &&
        (header->record_size == ERST_RECORD_SIZE) &&
        ((header->record_offset % ERST_RECORD_SIZE) == 0) &&
        (header->version == 0x0101) &&
        (header->reserved == 0x0000)
        )) {
        error_setg(errp, "ERST backend storage header is invalid");
    }

    /* Compute offset of first and last record storage slot */
    s->first_record_index = header->record_offset / ERST_RECORD_SIZE;
    s->last_record_index = (s->storage_size / ERST_RECORD_SIZE);
}

static void set_erst_map_by_index(ERSTDeviceState *s, unsigned index,
    uint64_t record_id)
{
    if (index < s->last_record_index) {
        s->header->map[index] = record_id;
    }
}

static unsigned lookup_erst_record(ERSTDeviceState *s,
    uint64_t record_identifier)
{
    unsigned rc = 0; /* 0 not a valid index */
    unsigned index = s->first_record_index;

    /* Find the record_identifier in the map */
    if (record_identifier != ERST_UNSPECIFIED_RECORD_ID) {
        /*
         * Count number of valid records encountered, and
         * short-circuit the loop if identifier not found
         */
        unsigned count = 0;
        for (; index < s->last_record_index &&
                count < s->header->record_count; ++index) {
            uint64_t map_record_identifier = s->header->map[index];
            if (map_record_identifier != ERST_UNSPECIFIED_RECORD_ID) {
                ++count;
            }
            if (map_record_identifier == record_identifier) {
                rc = index;
                break;
            }
        }
    } else {
        /* Find first available unoccupied slot */
        for (; index < s->last_record_index; ++index) {
            if (s->header->map[index] == ERST_UNSPECIFIED_RECORD_ID) {
                rc = index;
                break;
            }
        }
    }

    return rc;
}

/* ACPI 4.0: 17.4.2.3 Operations - Clearing */
static unsigned clear_erst_record(ERSTDeviceState *s)
{
    unsigned rc = STATUS_RECORD_NOT_FOUND;
    unsigned index;

    /* Check for valid record identifier */
    if (!ERST_IS_VALID_RECORD_ID(s->record_identifier)) {
        return STATUS_FAILED;
    }

    index = lookup_erst_record(s, s->record_identifier);
    if (index) {
        /* No need to wipe record, just invalidate its map entry */
        set_erst_map_by_index(s, index, ERST_UNSPECIFIED_RECORD_ID);
        s->header->record_count -= 1;
        rc = STATUS_SUCCESS;
    }

    return rc;
}

/* ACPI 4.0: 17.4.2.2 Operations - Reading */
static unsigned read_erst_record(ERSTDeviceState *s)
{
    unsigned rc = STATUS_RECORD_NOT_FOUND;
    unsigned index;

    /* Check record boundary wihin exchange buffer */
    if (s->record_offset >= (ERST_RECORD_SIZE - UEFI_CPER_RECORD_MIN_SIZE)) {
        return STATUS_FAILED;
    }

    /* Check for valid record identifier */
    if (!ERST_IS_VALID_RECORD_ID(s->record_identifier)) {
        return STATUS_FAILED;
    }

    index = lookup_erst_record(s, s->record_identifier);
    if (index) {
        uint8_t *ptr;
        uint8_t *record = ((uint8_t *)
            memory_region_get_ram_ptr(s->exchange_mr) +
            s->record_offset);
        ptr = get_nvram_ptr_by_index(s, index);
        memcpy(record, ptr, ERST_RECORD_SIZE - s->record_offset);
        rc = STATUS_SUCCESS;
    }

    return rc;
}

/* ACPI 4.0: 17.4.2.1 Operations - Writing */
static unsigned write_erst_record(ERSTDeviceState *s)
{
    unsigned rc = STATUS_FAILED;
    unsigned index;
    uint64_t record_identifier;
    uint8_t *record;
    uint8_t *ptr = NULL;
    bool record_found = false;

    /* Check record boundary wihin exchange buffer */
    if (s->record_offset >= (ERST_RECORD_SIZE - UEFI_CPER_RECORD_MIN_SIZE)) {
        return STATUS_FAILED;
    }

    /* Extract record identifier */
    record = ((uint8_t *)memory_region_get_ram_ptr(s->exchange_mr)
        + s->record_offset);
    record_identifier = THE_UEFI_CPER_RECORD_ID(record);

    /* Check for valid record identifier */
    if (!ERST_IS_VALID_RECORD_ID(record_identifier)) {
        return STATUS_FAILED;
    }

    index = lookup_erst_record(s, record_identifier);
    if (index) {
        /* Record found, overwrite existing record */
        ptr = get_nvram_ptr_by_index(s, index);
        record_found = true;
    } else {
        /* Record not found, not an overwrite, allocate for write */
        index = lookup_erst_record(s, ERST_UNSPECIFIED_RECORD_ID);
        if (index) {
            ptr = get_nvram_ptr_by_index(s, index);
        } else {
            rc = STATUS_NOT_ENOUGH_SPACE;
        }
    }
    if (ptr) {
        memcpy(ptr, record, ERST_RECORD_SIZE - s->record_offset);
        if (0 != s->record_offset) {
            memset(&ptr[ERST_RECORD_SIZE - s->record_offset],
                0xFF, s->record_offset);
        }
        if (!record_found) {
            s->header->record_count += 1; /* writing new record */
        }
        set_erst_map_by_index(s, index, record_identifier);
        rc = STATUS_SUCCESS;
    }

    return rc;
}

/* ACPI 4.0: 17.4.2.2 Operations - Reading "During boot..." */
static unsigned next_erst_record(ERSTDeviceState *s,
    uint64_t *record_identifier)
{
    unsigned rc = STATUS_RECORD_NOT_FOUND;
    unsigned index = s->next_record_index;

    *record_identifier = ERST_EMPTY_END_RECORD_ID;

    if (s->header->record_count) {
        for (; index < s->last_record_index; ++index) {
            uint64_t map_record_identifier;
            map_record_identifier = s->header->map[index];
            if (map_record_identifier != ERST_UNSPECIFIED_RECORD_ID) {
                    /* where to start next time */
                    s->next_record_index = index + 1;
                    *record_identifier = map_record_identifier;
                    rc = STATUS_SUCCESS;
                    break;
            }
        }
    }
    if (rc != STATUS_SUCCESS) {
        if (s->next_record_index == s->first_record_index) {
            /*
             * next_record_identifier is unchanged, no records found
             * and *record_identifier contains EMPTY_END id
             */
            rc = STATUS_RECORD_STORE_EMPTY;
        }
        /* at end/scan complete, reset */
        s->next_record_index = s->first_record_index;
    }

    return rc;
}

/*******************************************************************/

static uint64_t erst_rd_reg64(hwaddr addr,
    uint64_t reg, unsigned size)
{
    uint64_t rdval;
    uint64_t mask;
    unsigned shift;

    if (size == sizeof(uint64_t)) {
        /* 64b access */
        mask = 0xFFFFFFFFFFFFFFFFUL;
        shift = 0;
    } else {
        /* 32b access */
        mask = 0x00000000FFFFFFFFUL;
        shift = ((addr & 0x4) == 0x4) ? 32 : 0;
    }

    rdval = reg;
    rdval >>= shift;
    rdval &= mask;

    return rdval;
}

static uint64_t erst_wr_reg64(hwaddr addr,
    uint64_t reg, uint64_t val, unsigned size)
{
    uint64_t wrval;
    uint64_t mask;
    unsigned shift;

    if (size == sizeof(uint64_t)) {
        /* 64b access */
        mask = 0xFFFFFFFFFFFFFFFFUL;
        shift = 0;
    } else {
        /* 32b access */
        mask = 0x00000000FFFFFFFFUL;
        shift = ((addr & 0x4) == 0x4) ? 32 : 0;
    }

    val &= mask;
    val <<= shift;
    mask <<= shift;
    wrval = reg;
    wrval &= ~mask;
    wrval |= val;

    return wrval;
}

static void erst_reg_write(void *opaque, hwaddr addr,
    uint64_t val, unsigned size)
{
    ERSTDeviceState *s = (ERSTDeviceState *)opaque;

    /*
     * NOTE: All actions/operations/side effects happen on the WRITE,
     * by design. The READs simply return the reg_value contents.
     */
    trace_acpi_erst_reg_write(addr, val, size);

    switch (addr) {
    case ERST_VALUE_OFFSET + 0:
    case ERST_VALUE_OFFSET + 4:
        s->reg_value = erst_wr_reg64(addr, s->reg_value, val, size);
        break;
    case ERST_ACTION_OFFSET + 0:
/*  case ERST_ACTION_OFFSET+4: as coded, not really a 64b register */
        switch (val) {
        case ACTION_BEGIN_WRITE_OPERATION:
        case ACTION_BEGIN_READ_OPERATION:
        case ACTION_BEGIN_CLEAR_OPERATION:
        case ACTION_BEGIN_DUMMY_WRITE_OPERATION:
        case ACTION_END_OPERATION:
            s->operation = val;
            break;
        case ACTION_SET_RECORD_OFFSET:
            s->record_offset = s->reg_value;
            break;
        case ACTION_EXECUTE_OPERATION:
            if ((uint8_t)s->reg_value == ERST_EXECUTE_OPERATION_MAGIC) {
                s->busy_status = 1;
                switch (s->operation) {
                case ACTION_BEGIN_WRITE_OPERATION:
                    s->command_status = write_erst_record(s);
                    break;
                case ACTION_BEGIN_READ_OPERATION:
                    s->command_status = read_erst_record(s);
                    break;
                case ACTION_BEGIN_CLEAR_OPERATION:
                    s->command_status = clear_erst_record(s);
                    break;
                case ACTION_BEGIN_DUMMY_WRITE_OPERATION:
                    s->command_status = STATUS_SUCCESS;
                    break;
                case ACTION_END_OPERATION:
                    s->command_status = STATUS_SUCCESS;
                    break;
                default:
                    s->command_status = STATUS_FAILED;
                    break;
                }
                s->record_identifier = ERST_UNSPECIFIED_RECORD_ID;
                s->busy_status = 0;
            }
            break;
        case ACTION_CHECK_BUSY_STATUS:
            s->reg_value = s->busy_status;
            break;
        case ACTION_GET_COMMAND_STATUS:
            s->reg_value = s->command_status;
            break;
        case ACTION_GET_RECORD_IDENTIFIER:
            s->command_status = next_erst_record(s, &s->reg_value);
            break;
        case ACTION_SET_RECORD_IDENTIFIER:
            s->record_identifier = s->reg_value;
            break;
        case ACTION_GET_RECORD_COUNT:
            s->reg_value = s->header->record_count;
            break;
        case ACTION_GET_ERROR_LOG_ADDRESS_RANGE:
            s->reg_value = (hwaddr)pci_get_bar_addr(PCI_DEVICE(s), 1);
            break;
        case ACTION_GET_ERROR_LOG_ADDRESS_LENGTH:
            s->reg_value = ERST_RECORD_SIZE;
            break;
        case ACTION_GET_ERROR_LOG_ADDRESS_RANGE_ATTRIBUTES:
            s->reg_value = 0x0; /* intentional, not NVRAM mode */
            break;
        case ACTION_GET_EXECUTE_OPERATION_TIMINGS:
            s->reg_value =
                (100ULL << 32) | /* 100us max time */
                (10ULL  <<  0) ; /*  10us min time */
            break;
        default:
            /* Unknown action/command, NOP */
            break;
        }
        break;
    default:
        /* This should not happen, but if it does, NOP */
        break;
    }
}

static uint64_t erst_reg_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    ERSTDeviceState *s = (ERSTDeviceState *)opaque;
    uint64_t val = 0;

    switch (addr) {
    case ERST_ACTION_OFFSET + 0:
    case ERST_ACTION_OFFSET + 4:
        val = erst_rd_reg64(addr, s->reg_action, size);
        break;
    case ERST_VALUE_OFFSET + 0:
    case ERST_VALUE_OFFSET + 4:
        val = erst_rd_reg64(addr, s->reg_value, size);
        break;
    default:
        break;
    }
    trace_acpi_erst_reg_read(addr, val, size);
    return val;
}

static const MemoryRegionOps erst_reg_ops = {
    .read = erst_reg_read,
    .write = erst_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*******************************************************************/
/*******************************************************************/
static int erst_post_load(void *opaque, int version_id)
{
    ERSTDeviceState *s = opaque;

    /* Recompute pointer to header */
    s->header = (erst_storage_header_t *)get_nvram_ptr_by_index(s, 0);
    trace_acpi_erst_post_load(s->header);

    return 0;
}

static const VMStateDescription erst_vmstate  = {
    .name = "acpi-erst",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = erst_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(storage_size, ERSTDeviceState),
        VMSTATE_UINT8(operation, ERSTDeviceState),
        VMSTATE_UINT8(busy_status, ERSTDeviceState),
        VMSTATE_UINT8(command_status, ERSTDeviceState),
        VMSTATE_UINT32(record_offset, ERSTDeviceState),
        VMSTATE_UINT64(reg_action, ERSTDeviceState),
        VMSTATE_UINT64(reg_value, ERSTDeviceState),
        VMSTATE_UINT64(record_identifier, ERSTDeviceState),
        VMSTATE_UINT32(next_record_index, ERSTDeviceState),
        VMSTATE_UINT32(first_record_index, ERSTDeviceState),
        VMSTATE_UINT32(last_record_index, ERSTDeviceState),
        VMSTATE_END_OF_LIST()
    }
};

static void erst_realizefn(PCIDevice *pci_dev, Error **errp)
{
    ERSTDeviceState *s = ACPIERST(pci_dev);

    trace_acpi_erst_realizefn_in();

    if (!s->hostmem) {
        error_setg(errp, "'" ACPI_ERST_MEMDEV_PROP "' property is not set");
        return;
    } else if (host_memory_backend_is_mapped(s->hostmem)) {
        error_setg(errp, "can't use already busy memdev: %s",
                   object_get_canonical_path_component(OBJECT(s->hostmem)));
        return;
    }

    s->hostmem_mr = host_memory_backend_get_memory(s->hostmem);

    /* HostMemoryBackend size will be multiple of PAGE_SIZE */
    s->storage_size = object_property_get_int(OBJECT(s->hostmem), "size", errp);

    /* Check storage_size against ERST_RECORD_SIZE */
    if (((s->storage_size % ERST_RECORD_SIZE) != 0) ||
         (ERST_RECORD_SIZE > s->storage_size)) {
        error_setg(errp, "ACPI ERST requires size be multiple of "
            "record size (%luKiB)", ERST_RECORD_SIZE);
    }

    /* Initialize backend storage and record_count */
    check_erst_backend_storage(s, errp);

    /* BAR 0: Programming registers */
    memory_region_init_io(&s->iomem, OBJECT(pci_dev), &erst_reg_ops, s,
                          TYPE_ACPI_ERST, ERST_REG_SIZE);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->iomem);

    /* BAR 1: Exchange buffer memory */
    /* Create a hostmem object to use as the exchange buffer */
    s->exchange_obj = object_new(TYPE_MEMORY_BACKEND_RAM);
    object_property_set_int(s->exchange_obj, "size", ERST_RECORD_SIZE, errp);
    user_creatable_complete(USER_CREATABLE(s->exchange_obj), errp);
    s->exchange = MEMORY_BACKEND(s->exchange_obj);
    host_memory_backend_set_mapped(s->exchange, true);
    s->exchange_mr = host_memory_backend_get_memory(s->exchange);
    memory_region_init_resizeable_ram(s->exchange_mr, OBJECT(pci_dev),
        TYPE_ACPI_ERST, ERST_RECORD_SIZE, ERST_RECORD_SIZE, NULL, errp);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, s->exchange_mr);
    /* Include the exchange buffer in the migration stream */
    vmstate_register_ram_global(s->exchange_mr);

    /* Include the backend storage in the migration stream */
    vmstate_register_ram_global(s->hostmem_mr);

    trace_acpi_erst_realizefn_out(s->storage_size);
}

static void erst_reset(DeviceState *dev)
{
    ERSTDeviceState *s = ACPIERST(dev);

    trace_acpi_erst_reset_in(s->header->record_count);
    s->operation = 0;
    s->busy_status = 0;
    s->command_status = STATUS_SUCCESS;
    s->record_identifier = ERST_UNSPECIFIED_RECORD_ID;
    s->record_offset = 0;
    s->next_record_index = s->first_record_index;
    /* NOTE: first/last_record_index are computed only once */
    trace_acpi_erst_reset_out(s->header->record_count);
}

static Property erst_properties[] = {
    DEFINE_PROP_LINK(ACPI_ERST_MEMDEV_PROP, ERSTDeviceState, hostmem,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
    DEFINE_PROP_END_OF_LIST(),
};

static void erst_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    trace_acpi_erst_class_init_in();
    k->realize = erst_realizefn;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_ACPI_ERST;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_OTHERS;
    dc->reset = erst_reset;
    dc->vmsd = &erst_vmstate;
    dc->user_creatable = true;
    device_class_set_props(dc, erst_properties);
    dc->desc = "ACPI Error Record Serialization Table (ERST) device";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    trace_acpi_erst_class_init_out();
}

static const TypeInfo erst_type_info = {
    .name          = TYPE_ACPI_ERST,
    .parent        = TYPE_PCI_DEVICE,
    .class_init    = erst_class_init,
    .instance_size = sizeof(ERSTDeviceState),
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

static void erst_register_types(void)
{
    type_register_static(&erst_type_info);
}

type_init(erst_register_types)
