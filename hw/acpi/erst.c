/*
 * ACPI Error Record Serialization Table, ERST, Implementation
 *
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * See ACPI specification,
 * "ACPI Platform Error Interfaces" : "Error Serialization"
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/bios-linker-loader.h"
#include "exec/address-spaces.h"
#include "hw/acpi/erst.h"

#ifdef _ERST_DEBUG
#define erst_debug(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); fflush(stderr); } while (0)
#else
#define erst_debug(fmt, ...) do { } while (0)
#endif

/* See UEFI spec, Appendix N Common Platform Error Record */
/* UEFI CPER allows for an OSPM book keeping area in the record */
#define UEFI_CPER_RECORD_MIN_SIZE 128U
#define UEFI_CPER_SIZE_OFFSET 20U
#define UEFI_CPER_RECORD_ID_OFFSET 96U
#define IS_UEFI_CPER_RECORD(ptr) \
    (((ptr)[0] == 'C') && \
     ((ptr)[1] == 'P') && \
     ((ptr)[2] == 'E') && \
     ((ptr)[3] == 'R'))
#define THE_UEFI_CPER_RECORD_ID(ptr) \
    (*(uint64_t *)(&(ptr)[UEFI_CPER_RECORD_ID_OFFSET]))

#define ERST_INVALID_RECORD_ID (~0UL)
#define ERST_EXECUTE_OPERATION_MAGIC 0x9CUL
#define ERST_CSR_ACTION (0UL << 3) /* action (cmd) */
#define ERST_CSR_VALUE  (1UL << 3) /* argument/value (data) */

/*
 * As ERST_IOMEM_SIZE is used to map the ERST into the guest,
 * it should/must be an integer multiple of PAGE_SIZE.
 * NOTE that any change to this value will make any pre-
 * existing backing files, not of the same ERST_IOMEM_SIZE,
 * unusable to the guest.
 */
#define ERST_IOMEM_SIZE (2UL * 4096)

/*
 * This implementation is an ACTION (cmd) and VALUE (data)
 * interface consisting of just two 64-bit registers.
 */
#define ERST_REG_LEN (2UL * sizeof(uint64_t))

/*
 * The space not utilized by the register interface is the
 * buffer for exchanging ERST record contents.
 */
#define ERST_RECORD_SIZE (ERST_IOMEM_SIZE - ERST_REG_LEN)

/*
 * Mode to be used for backing file
 */
#define ERST_BACKING_FILE_MODE 0644 /* S_IRWXU|S_IRWXG */

#define ACPIERST(obj) \
    OBJECT_CHECK(ERSTDeviceState, (obj), TYPE_ACPI_ERST)
#define ACPIERST_CLASS(oc) \
    OBJECT_CLASS_CHECK(ERSTDeviceStateClass, (oc), TYPE_ACPI_ERST)
#define ACPIERST_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ERSTDeviceStateClass, (obj), TYPE_ACPI_ERST)

static hwaddr erst_base;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t prop_size;
    char *prop_filename;
    hwaddr base;

    uint8_t operation;
    uint8_t busy_status;
    uint8_t command_status;
    uint32_t record_offset;
    uint32_t record_count;
    uint64_t reg_action;
    uint64_t reg_value;
    uint64_t record_identifier;

    unsigned next_record_index;
    uint8_t record[ERST_RECORD_SIZE]; /* read/written directly by guest */
    uint8_t tmp_record[ERST_RECORD_SIZE]; /* intermediate manipulation buffer */
    uint8_t *nvram; /* persistent storage, of length prop_size */

} ERSTDeviceState;

static void update_erst_backing_file(ERSTDeviceState *s,
    off_t offset, const uint8_t *data, size_t length)
{
    int fd;

    /* Bounds check */
    if ((offset + length) > s->prop_size) {
        error_report("update: off 0x%lx len 0x%lx > size 0x%lx out of range",
            (long)offset, (long)length, (long)s->prop_size);
        return;
    }

    fd = open(s->prop_filename, O_WRONLY | O_CREAT, ERST_BACKING_FILE_MODE);
    if (fd > 0) {
        off_t src;
        size_t wrc = 0;
        src = lseek(fd, offset, SEEK_SET);
        if (offset == src) {
            wrc = write(fd, data, length);
        }
        if ((offset != src) || (length != wrc)) {
            error_report("ERST write failed: %d %d", (int)wrc, (int)length);
        }
        close(fd);
    } else {
        error_report("open failed: %s : %d", s->prop_filename, fd);
    }
}

static unsigned copy_from_nvram_by_index(ERSTDeviceState *s, unsigned index)
{
    /* Read a nvram[] entry into tmp_record */
    unsigned rc = ACPI_ERST_STATUS_FAILED;
    off_t offset = (index * ERST_RECORD_SIZE);

    if ((offset + ERST_RECORD_SIZE) <= s->prop_size) {
        memcpy(s->tmp_record, &s->nvram[offset], ERST_RECORD_SIZE);
        rc = ACPI_ERST_STATUS_SUCCESS;
    }
    return rc;
}

static unsigned copy_to_nvram_by_index(ERSTDeviceState *s, unsigned index)
{
    /* Write entry in tmp_record into nvram[], and backing file */
    unsigned rc = ACPI_ERST_STATUS_FAILED;
    off_t offset = (index * ERST_RECORD_SIZE);

    if ((offset + ERST_RECORD_SIZE) <= s->prop_size) {
        memcpy(&s->nvram[offset], s->tmp_record, ERST_RECORD_SIZE);
        update_erst_backing_file(s, offset, s->tmp_record, ERST_RECORD_SIZE);
        rc = ACPI_ERST_STATUS_SUCCESS;
    }
    return rc;
}

static int lookup_erst_record_by_identifier(ERSTDeviceState *s,
    uint64_t record_identifier, bool *record_found, bool alloc_for_write)
{
    int rc = -1;
    int empty_index = -1;
    int index = 0;
    unsigned rrc;

    *record_found = 0;

    do {
        rrc = copy_from_nvram_by_index(s, (unsigned)index);
        if (rrc == ACPI_ERST_STATUS_SUCCESS) {
            uint64_t this_identifier;
            this_identifier = THE_UEFI_CPER_RECORD_ID(s->tmp_record);
            if (IS_UEFI_CPER_RECORD(s->tmp_record) &&
                (this_identifier == record_identifier)) {
                rc = index;
                *record_found = 1;
                break;
            }
            if ((this_identifier == ERST_INVALID_RECORD_ID) &&
                (empty_index < 0)) {
                empty_index = index; /* first available for write */
            }
        }
        ++index;
    } while (rrc == ACPI_ERST_STATUS_SUCCESS);

    /* Record not found, allocate for writing */
    if ((rc < 0) && alloc_for_write) {
        rc = empty_index;
    }

    return rc;
}

static unsigned clear_erst_record(ERSTDeviceState *s)
{
    unsigned rc = ACPI_ERST_STATUS_RECORD_NOT_FOUND;
    bool record_found;
    int index;

    index = lookup_erst_record_by_identifier(s,
        s->record_identifier, &record_found, 0);
    if (record_found) {
        memset(s->tmp_record, 0xFF, ERST_RECORD_SIZE);
        rc = copy_to_nvram_by_index(s, (unsigned)index);
        if (rc == ACPI_ERST_STATUS_SUCCESS) {
            s->record_count -= 1;
        }
    }

    return rc;
}

static unsigned write_erst_record(ERSTDeviceState *s)
{
    unsigned rc = ACPI_ERST_STATUS_FAILED;

    if (s->record_offset < (ERST_RECORD_SIZE - UEFI_CPER_RECORD_MIN_SIZE)) {
        uint64_t record_identifier;
        uint8_t *record = &s->record[s->record_offset];
        bool record_found;
        int index;

        record_identifier = (s->record_identifier == ERST_INVALID_RECORD_ID)
            ? THE_UEFI_CPER_RECORD_ID(record) : s->record_identifier;

        index = lookup_erst_record_by_identifier(s,
            record_identifier, &record_found, 1);
        if (index < 0) {
            rc = ACPI_ERST_STATUS_NOT_ENOUGH_SPACE;
        } else {
            if (0 != s->record_offset) {
                memset(&s->tmp_record[ERST_RECORD_SIZE - s->record_offset],
                    0xFF, s->record_offset);
            }
            memcpy(s->tmp_record, record, ERST_RECORD_SIZE - s->record_offset);
            rc = copy_to_nvram_by_index(s, (unsigned)index);
            if (rc == ACPI_ERST_STATUS_SUCCESS) {
                if (!record_found) { /* not overwriting existing record */
                    s->record_count += 1; /* writing new record */
                }
            }
        }
    }

    return rc;
}

static unsigned next_erst_record(ERSTDeviceState *s,
    uint64_t *record_identifier)
{
    unsigned rc = ACPI_ERST_STATUS_RECORD_NOT_FOUND;
    unsigned index;
    unsigned rrc;

    *record_identifier = ERST_INVALID_RECORD_ID;

    index = s->next_record_index;
    do {
        rrc = copy_from_nvram_by_index(s, (unsigned)index);
        if (rrc == ACPI_ERST_STATUS_SUCCESS) {
            if (IS_UEFI_CPER_RECORD(s->tmp_record)) {
                s->next_record_index = index + 1; /* where to start next time */
                *record_identifier = THE_UEFI_CPER_RECORD_ID(s->tmp_record);
                rc = ACPI_ERST_STATUS_SUCCESS;
                break;
            }
            ++index;
        } else {
            if (s->next_record_index == 0) {
                rc = ACPI_ERST_STATUS_RECORD_STORE_EMPTY;
            }
            s->next_record_index = 0; /* at end, reset */
        }
    } while (rrc == ACPI_ERST_STATUS_SUCCESS);

    return rc;
}

static unsigned read_erst_record(ERSTDeviceState *s)
{
    unsigned rc = ACPI_ERST_STATUS_RECORD_NOT_FOUND;
    bool record_found;
    int index;

    index = lookup_erst_record_by_identifier(s,
        s->record_identifier, &record_found, 0);
    if (record_found) {
        rc = copy_from_nvram_by_index(s, (unsigned)index);
        if (rc == ACPI_ERST_STATUS_SUCCESS) {
            if (s->record_offset < ERST_RECORD_SIZE) {
                memcpy(&s->record[s->record_offset], s->tmp_record,
                    ERST_RECORD_SIZE - s->record_offset);
            }
        }
    }

    return rc;
}

static unsigned get_erst_record_count(ERSTDeviceState *s)
{
    /* Compute record_count */
    off_t offset;

    s->record_count = 0;
    offset = 0;
    do {
        uint8_t *ptr = &s->nvram[offset];
        uint64_t record_identifier = THE_UEFI_CPER_RECORD_ID(ptr);
        if (IS_UEFI_CPER_RECORD(ptr) &&
            (ERST_INVALID_RECORD_ID != record_identifier)) {
            s->record_count += 1;
        }
        offset += ERST_RECORD_SIZE;
    } while (offset < (off_t)s->prop_size);

    return s->record_count;
}

static void load_erst_backing_file(ERSTDeviceState *s)
{
    int fd;
    struct stat statbuf;

    erst_debug("+load_erst_backing_file()\n");

    /* Allocate and initialize nvram[] */
    s->nvram = g_malloc(s->prop_size);
    memset(s->nvram, 0xFF, s->prop_size);

    /* Ensure backing file at least same as prop_size */
    if (stat(s->prop_filename, &statbuf) == 0) {
        /* ensure prop_size at least matches file size */
        if (statbuf.st_size < s->prop_size) {
            /* Ensure records are ERST_INVALID_RECORD_ID */
            memset(s->nvram, 0xFF, s->prop_size - statbuf.st_size);
            update_erst_backing_file(s,
                statbuf.st_size, s->nvram, s->prop_size - statbuf.st_size);
        }
    }

    /* Pre-load nvram[] from backing file, if present */
    fd = open(s->prop_filename, O_RDONLY, ERST_BACKING_FILE_MODE);
    if (fd > 0) {
        size_t rrc = read(fd, s->nvram, s->prop_size);
        (void)rrc;
        close(fd);
        /*
         * If existing file is smaller than prop_size, it will be resized
         * accordingly upon subsequent record writes. If the file
         * is larger than prop_size, only prop_size bytes are utilized,
         * the extra bytes are untouched (though will be lost after
         * a migration when the backing file is re-written as length
         * of prop_size bytes).
         */
    } else {
        /* Create empty backing file */
        update_erst_backing_file(s, 0, s->nvram, s->prop_size);
    }

    /* Initialize record_count */
    get_erst_record_count(s);

    erst_debug("-load_erst_backing_file() %d\n", s->record_count);
}

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

static void erst_write(void *opaque, hwaddr addr,
    uint64_t val, unsigned size)
{
    ERSTDeviceState *s = (ERSTDeviceState *)opaque;

    if (addr < ERST_REG_LEN) {
        /*
         * NOTE: All actions/operations/side effects happen on the WRITE,
         * by design. The READs simply return the reg_value contents.
         */
        erst_debug("ERST write %016lx %10s val %016lx sz %u",
            addr, erst_reg(addr), val, size);
        /* The REGISTER region */
        switch (addr) {
        case ERST_CSR_VALUE + 0:
        case ERST_CSR_VALUE + 4:
            s->reg_value = erst_wr_reg64(addr, s->reg_value, val, size);
            break;
        case ERST_CSR_ACTION + 0:
/*      case ERST_CSR_ACTION+4: as coded, not really a 64b register */
            switch (val) {
            case ACPI_ERST_ACTION_BEGIN_WRITE_OPERATION:
            case ACPI_ERST_ACTION_BEGIN_READ_OPERATION:
            case ACPI_ERST_ACTION_BEGIN_CLEAR_OPERATION:
            case ACPI_ERST_ACTION_BEGIN_DUMMY_WRITE_OPERATION:
            case ACPI_ERST_ACTION_END_OPERATION:
                s->operation = val;
                break;
            case ACPI_ERST_ACTION_SET_RECORD_OFFSET:
                s->record_offset = s->reg_value;
                break;
            case ACPI_ERST_ACTION_EXECUTE_OPERATION:
                if ((uint8_t)s->reg_value == ERST_EXECUTE_OPERATION_MAGIC) {
                    s->busy_status = 1;
                    switch (s->operation) {
                    case ACPI_ERST_ACTION_BEGIN_WRITE_OPERATION:
                        s->command_status = write_erst_record(s);
                        break;
                    case ACPI_ERST_ACTION_BEGIN_READ_OPERATION:
                        s->command_status = read_erst_record(s);
                        break;
                    case ACPI_ERST_ACTION_BEGIN_CLEAR_OPERATION:
                        s->command_status = clear_erst_record(s);
                        break;
                    case ACPI_ERST_ACTION_BEGIN_DUMMY_WRITE_OPERATION:
                        s->command_status = ACPI_ERST_STATUS_SUCCESS;
                        break;
                    case ACPI_ERST_ACTION_END_OPERATION:
                        s->command_status = ACPI_ERST_STATUS_SUCCESS;
                        break;
                    default:
                        s->command_status = ACPI_ERST_STATUS_FAILED;
                        break;
                    }
                    s->record_identifier = ERST_INVALID_RECORD_ID;
                    s->busy_status = 0;
                }
                break;
            case ACPI_ERST_ACTION_CHECK_BUSY_STATUS:
                s->reg_value = s->busy_status;
                break;
            case ACPI_ERST_ACTION_GET_COMMAND_STATUS:
                s->reg_value = s->command_status;
                break;
            case ACPI_ERST_ACTION_GET_RECORD_IDENTIFIER:
                s->command_status = next_erst_record(s, &s->reg_value);
                break;
            case ACPI_ERST_ACTION_SET_RECORD_IDENTIFIER:
                s->record_identifier = s->reg_value;
                break;
            case ACPI_ERST_ACTION_GET_RECORD_COUNT:
                s->reg_value = s->record_count;
                break;
            case ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_RANGE:
                s->reg_value = s->base + ERST_REG_LEN;
                break;
            case ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_LENGTH:
                s->reg_value = ERST_RECORD_SIZE;
                break;
            case ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_RANGE_ATTRIBUTES:
                s->reg_value = 0; /* correct/intended value */
                break;
            case ACPI_ERST_ACTION_GET_EXECUTE_OPERATION_TIMINGS:
                /*
                 * 100UL is max, 10UL is nominal
                 */
                s->reg_value = ((100UL << 32) | (10UL << 0));
                break;
            case ACPI_ERST_ACTION_RESERVED:
            default:
                /*
                 * NOP
                 */
                break;
            }
            break;
        default:
            /*
             * All other register writes are NO-OPs
             */
            break;
        }
    } else {
        /* The RECORD region */
        unsigned offset = addr - ERST_REG_LEN;
        uint8_t *ptr = &s->record[offset];
        switch (size) {
        default:
        case sizeof(uint8_t):
            *(uint8_t *)ptr = (uint8_t)val;
            break;
        case sizeof(uint16_t):
            *(uint16_t *)ptr = (uint16_t)val;
            break;
        case sizeof(uint32_t):
            *(uint32_t *)ptr = (uint32_t)val;
            break;
        case sizeof(uint64_t):
            *(uint64_t *)ptr = (uint64_t)val;
            break;
        }
    }
}

static uint64_t erst_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    ERSTDeviceState *s = (ERSTDeviceState *)opaque;
    uint64_t val = 0;

    if (addr < ERST_REG_LEN) {
        switch (addr) {
        case ERST_CSR_ACTION + 0:
        case ERST_CSR_ACTION + 4:
            val = erst_rd_reg64(addr, s->reg_action, size);
            break;
        case ERST_CSR_VALUE + 0:
        case ERST_CSR_VALUE + 4:
            val = erst_rd_reg64(addr, s->reg_value, size);
            break;
        default:
            break;
        }
    erst_debug("ERST read  %016lx %10s val %016lx sz %u\n",
        addr, erst_reg(addr), val, size);
    } else {
        /*
         * The RECORD region
         */
        uint8_t *ptr = &s->record[addr - ERST_REG_LEN];
        switch (size) {
        default:
        case sizeof(uint8_t):
            val = *(uint8_t *)ptr;
            break;
        case sizeof(uint16_t):
            val = *(uint16_t *)ptr;
            break;
        case sizeof(uint32_t):
            val = *(uint32_t *)ptr;
            break;
        case sizeof(uint64_t):
            val = *(uint64_t *)ptr;
            break;
        }
    }
    erst_debug("ERST read  %016lx %10s val %016lx sz %u\n",
        addr, erst_reg(addr), val, size);
    return val;
}

static size_t build_erst_action(GArray *table_data,
    uint8_t serialization_action,
    uint8_t instruction,
    uint8_t flags,
    uint8_t width,
    uint64_t address,
    uint64_t value,
    uint64_t mask)
{
    /* See ACPI spec, Error Serialization */
    uint8_t access_width = 0;
    build_append_int_noprefix(table_data, serialization_action, 1);
    build_append_int_noprefix(table_data, instruction         , 1);
    build_append_int_noprefix(table_data, flags               , 1);
    build_append_int_noprefix(table_data, 0                   , 1);
    /* GAS space_id */
    build_append_int_noprefix(table_data, AML_SYSTEM_MEMORY   , 1);
    /* GAS bit_width */
    build_append_int_noprefix(table_data, width               , 1);
    /* GAS bit_offset */
    build_append_int_noprefix(table_data, 0                   , 1);
    /* GAS access_width */
    switch (width) {
    case 8:
        access_width = 1;
        break;
    case 16:
        access_width = 2;
        break;
    case 32:
        access_width = 3;
        break;
    case 64:
        access_width = 4;
        break;
    default:
        access_width = 0;
        break;
    }
    build_append_int_noprefix(table_data, access_width        , 1);
    /* GAS address */
    build_append_int_noprefix(table_data, address, 8);
    /* value */
    build_append_int_noprefix(table_data, value  , 8);
    /* mask */
    build_append_int_noprefix(table_data, mask   , 8);

    return 1;
}

static const MemoryRegionOps erst_rw_ops = {
    .read = erst_read,
    .write = erst_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void build_erst(GArray *table_data, BIOSLinker *linker, hwaddr base)
{
    unsigned action, insns = 0;
    unsigned erst_start = table_data->len;
    unsigned iec_offset = 0;

    /* See ACPI spec, Error Serialization */
    acpi_data_push(table_data, sizeof(AcpiTableHeader));
    /* serialization_header_length */
    build_append_int_noprefix(table_data, 48, 4);
    /* reserved */
    build_append_int_noprefix(table_data,  0, 4);
    iec_offset = table_data->len;
    /* instruction_entry_count (placeholder) */
    build_append_int_noprefix(table_data,  0, 4);

#define BEA(I, F, W, ADDR, VAL, MASK) \
    build_erst_action(table_data, action, \
        ACPI_ERST_INST_##I, F, W, base + ADDR, VAL, MASK)
#define MASK8  0x00000000000000FFUL
#define MASK16 0x000000000000FFFFUL
#define MASK32 0x00000000FFFFFFFFUL
#define MASK64 0xFFFFFFFFFFFFFFFFUL

    for (action = 0; action < ACPI_ERST_MAX_ACTIONS; ++action) {
        switch (action) {
        case ACPI_ERST_ACTION_BEGIN_WRITE_OPERATION:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_BEGIN_READ_OPERATION:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_BEGIN_CLEAR_OPERATION:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_END_OPERATION:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_SET_RECORD_OFFSET:
            insns += BEA(WRITE_REGISTER      , 0, 32,
                ERST_CSR_VALUE , 0, MASK32);
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_EXECUTE_OPERATION:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_VALUE , ERST_EXECUTE_OPERATION_MAGIC, MASK8);
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_CHECK_BUSY_STATUS:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            insns += BEA(READ_REGISTER_VALUE , 0, 32,
                ERST_CSR_VALUE, 0x01, MASK8);
            break;
        case ACPI_ERST_ACTION_GET_COMMAND_STATUS:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            insns += BEA(READ_REGISTER       , 0, 32,
                ERST_CSR_VALUE, 0, MASK8);
            break;
        case ACPI_ERST_ACTION_GET_RECORD_IDENTIFIER:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            insns += BEA(READ_REGISTER       , 0, 64,
                ERST_CSR_VALUE, 0, MASK64);
            break;
        case ACPI_ERST_ACTION_SET_RECORD_IDENTIFIER:
            insns += BEA(WRITE_REGISTER      , 0, 64,
                ERST_CSR_VALUE , 0, MASK64);
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_GET_RECORD_COUNT:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            insns += BEA(READ_REGISTER       , 0, 32,
                ERST_CSR_VALUE, 0, MASK32);
            break;
        case ACPI_ERST_ACTION_BEGIN_DUMMY_WRITE_OPERATION:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_RESERVED:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            break;
        case ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_RANGE:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            insns += BEA(READ_REGISTER       , 0, 64,
                ERST_CSR_VALUE, 0, MASK64);
            break;
        case ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_LENGTH:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            insns += BEA(READ_REGISTER       , 0, 64,
                ERST_CSR_VALUE, 0, MASK32);
            break;
        case ACPI_ERST_ACTION_GET_ERROR_LOG_ADDRESS_RANGE_ATTRIBUTES:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            insns += BEA(READ_REGISTER       , 0, 32,
                ERST_CSR_VALUE, 0, MASK32);
            break;
        case ACPI_ERST_ACTION_GET_EXECUTE_OPERATION_TIMINGS:
            insns += BEA(WRITE_REGISTER_VALUE, 0, 32,
                ERST_CSR_ACTION, action, MASK8);
            insns += BEA(READ_REGISTER       , 0, 64,
                ERST_CSR_VALUE, 0, MASK64);
        default:
            insns += BEA(NOOP, 0, 0, 0, action, 0);
            break;
        }
    }

    /* acpi_data_push() within BEA() can result in new GArray pointer */
    *(uint32_t *)(table_data->data + iec_offset) = cpu_to_le32(insns);

    build_header(linker, table_data,
                 (void *)(table_data->data + erst_start),
                 "ERST", table_data->len - erst_start,
                 1, NULL, NULL);

    if (erst_base == 0) {
        /*
         * This ACPI routine is invoked twice, but this code
         * snippet needs to happen just once.
         * And this code in erst_class_init() is too early.
         */
        DeviceState *dev;
        SysBusDevice *s;

        dev = qdev_new(TYPE_ACPI_ERST);
        erst_debug("qdev_create dev %p\n", dev);
        s = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(s, &error_fatal);

        ACPIERST(dev)->base = base;
        sysbus_mmio_map(s, 0, base);
        erst_base = base;
        erst_debug("erst_base %lx\n", base);
    }
}

/*******************************************************************/
/*******************************************************************/
static int erst_post_load(void *opaque, int version_id)
{
    ERSTDeviceState *s = opaque;
    erst_debug("+erst_post_load(%d)\n", version_id);
    /* Ensure nvram[] persists into backing file */
    update_erst_backing_file(s, 0, s->nvram, s->prop_size);
    erst_debug("-erst_post_load()\n");
    return 0;
}

static const VMStateDescription erst_vmstate  = {
    .name = "acpi-erst",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = erst_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(operation, ERSTDeviceState),
        VMSTATE_UINT8(busy_status, ERSTDeviceState),
        VMSTATE_UINT8(command_status, ERSTDeviceState),
        VMSTATE_UINT32(record_offset, ERSTDeviceState),
        VMSTATE_UINT32(record_count, ERSTDeviceState),
        VMSTATE_UINT64(reg_action, ERSTDeviceState),
        VMSTATE_UINT64(reg_value, ERSTDeviceState),
        VMSTATE_UINT64(record_identifier, ERSTDeviceState),
        VMSTATE_UINT8_ARRAY(record, ERSTDeviceState, ERST_RECORD_SIZE),
        VMSTATE_UINT8_ARRAY(tmp_record, ERSTDeviceState, ERST_RECORD_SIZE),
        VMSTATE_VARRAY_UINT32(nvram, ERSTDeviceState, prop_size, 0,
            vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

static void erst_realizefn(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    ERSTDeviceState *s = ACPIERST(dev);

    erst_debug("+erst_realizefn()\n");
    if (!s->prop_filename) {
        s->prop_filename = (char *)(TYPE_ACPI_ERST ".backing");
    }

    if (!s->prop_filename) {
        error_setg(errp, "'filename' property is not set");
        return;
    }

    if (!(s->prop_size > ERST_RECORD_SIZE) &&
        (s->prop_size <= 0x04000000)) {
        error_setg(errp, "'size' property %d is not set properly",
            s->prop_size);
        return;
    }

    /* Convert prop_size to integer multiple of ERST_RECORD_SIZE */
    s->prop_size -= (s->prop_size % ERST_RECORD_SIZE);

    load_erst_backing_file(s);

    erst_debug("filename %s\n", s->prop_filename);
    erst_debug("size %x\n", s->prop_size);

    memory_region_init_io(&s->iomem, OBJECT(s), &erst_rw_ops, s,
                          TYPE_ACPI_ERST, ERST_IOMEM_SIZE);
    sysbus_init_mmio(d, &s->iomem);
    erst_debug("-erst_realizefn()\n");
}

static void erst_unrealizefn(DeviceState *dev)
{
    ERSTDeviceState *s = ACPIERST(dev);

    erst_debug("+erst_unrealizefn()\n");
    if (s->nvram) {
        /* Ensure nvram[] persists into backing file */
        update_erst_backing_file(s, 0, s->nvram, s->prop_size);
        g_free(s->nvram);
        s->nvram = NULL;
    }
    erst_debug("-erst_unrealizefn()\n");
}

static void erst_reset(DeviceState *dev)
{
    ERSTDeviceState *s = ACPIERST(dev);

    erst_debug("+erst_reset(%p) %d\n", s, s->record_count);
    s->operation = 0;
    s->busy_status = 0;
    s->command_status = ACPI_ERST_STATUS_SUCCESS;
    /* indicate empty/no-more until further notice */
    s->record_identifier = ERST_INVALID_RECORD_ID;
    s->record_offset = 0;
    s->next_record_index = 0;
    /* NOTE: record_count and nvram[] are initialized elsewhere */
    erst_debug("-erst_reset()\n");
}

static Property erst_properties[] = {
    DEFINE_PROP_UINT32("size", ERSTDeviceState, prop_size, 0x00010000),
    DEFINE_PROP_STRING("filename", ERSTDeviceState, prop_filename),
    DEFINE_PROP_END_OF_LIST(),
};

static void erst_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    erst_debug("+erst_class_init()\n");
    dc->realize = erst_realizefn;
    dc->unrealize = erst_unrealizefn;
    dc->reset = erst_reset;
    dc->vmsd = &erst_vmstate;
    device_class_set_props(dc, erst_properties);
    dc->desc = "ACPI Error Record Serialization Table (ERST) device";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    erst_debug("-erst_class_init()\n");
}

static const TypeInfo erst_type_info = {
    .name          = TYPE_ACPI_ERST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .class_init    = erst_class_init,
    .instance_size = sizeof(ERSTDeviceState),
};

static void erst_register_types(void)
{
    type_register_static(&erst_type_info);
}

type_init(erst_register_types)
