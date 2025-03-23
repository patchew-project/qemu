/*
 * Firmware Assisted Dump in PSeries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PPC_SPAPR_FADUMP_H
#define PPC_SPAPR_FADUMP_H

#include "qemu/osdep.h"
#include "cpu.h"

/* Fadump commands */
#define FADUMP_CMD_REGISTER            1
#define FADUMP_CMD_UNREGISTER          2
#define FADUMP_CMD_INVALIDATE          3

#define FADUMP_VERSION                 1

/* Firmware provided dump sections */
#define FADUMP_CPU_STATE_DATA   0x0001
#define FADUMP_HPTE_REGION      0x0002
#define FADUMP_REAL_MODE_REGION 0x0011

/* OS defined sections */
#define FADUMP_PARAM_AREA       0x0100

/* Dump request flag */
#define FADUMP_REQUEST_FLAG     0x00000001

/* Dump status flags */
#define FADUMP_STATUS_DUMP_PERFORMED            0x8000
#define FADUMP_STATUS_DUMP_TRIGGERED            0x4000
#define FADUMP_STATUS_DUMP_ERROR                0x2000

/* Region dump error flags */
#define FADUMP_ERROR_INVALID_DATA_TYPE          0x8000
#define FADUMP_ERROR_INVALID_SOURCE_ADDR        0x4000
#define FADUMP_ERROR_LENGTH_EXCEEDS_SOURCE      0x2000
#define FADUMP_ERROR_INVALID_DEST_ADDR          0x1000
#define FAUDMP_ERROR_DEST_TOO_SMALL             0x0800

/*
 * The Firmware Assisted Dump Memory structure supports a maximum of 10 sections
 * in the dump memory structure. Presently, three sections are used for
 * CPU state data, HPTE & Parameters area, while the remaining seven sections
 * can be used for boot memory regions.
 */
#define FADUMP_MAX_SECTIONS            10
#define RTAS_FADUMP_MAX_BOOT_MEM_REGS  7

/* Number of registers stored per cpu */
#define FADUMP_NUM_PER_CPU_REGS (32 /*GPR*/ + 45 /*others*/ + 2 /*STRT & END*/)

typedef struct FadumpSection FadumpSection;
typedef struct FadumpSectionHeader FadumpSectionHeader;
typedef struct FadumpMemStruct FadumpMemStruct;
typedef struct FadumpRegSaveAreaHeader FadumpRegSaveAreaHeader;
typedef struct FadumpRegEntry FadumpRegEntry;

struct SpaprMachineState;

/* Kernel Dump section info */
struct FadumpSection {
    __be32    request_flag;
    __be16    source_data_type;
    __be16    error_flags;
    __be64    source_address;
    __be64    source_len;
    __be64    bytes_dumped;
    __be64    destination_address;
};

/* ibm,configure-kernel-dump header. */
struct FadumpSectionHeader {
    __be32    dump_format_version;
    __be16    dump_num_sections;
    __be16    dump_status_flag;
    __be32    offset_first_dump_section;

    /* Fields for disk dump option. */
    __be32    dd_block_size;
    __be64    dd_block_offset;
    __be64    dd_num_blocks;
    __be32    dd_offset_disk_path;

    /* Maximum time allowed to prevent an automatic dump-reboot. */
    __be32    max_time_auto;
};

/* Note: All the data in these structures is in big-endian */
struct FadumpMemStruct {
    FadumpSectionHeader header;
    FadumpSection       rgn[FADUMP_MAX_SECTIONS];
};

/*
 * The firmware-assisted dump format.
 *
 * The register save area is an area in the partition's memory used to preserve
 * the register contents (CPU state data) for the active CPUs during a firmware
 * assisted dump. The dump format contains register save area header followed
 * by register entries. Each list of registers for a CPU starts with "CPUSTRT"
 * and ends with "CPUEND".
 */

/* Register save area header. */
struct FadumpRegSaveAreaHeader {
    __be64    magic_number;
    __be32    version;
    __be32    num_cpu_offset;
};

/* Register entry. */
struct FadumpRegEntry {
    __be64    reg_id;
    __be64    reg_value;
};

uint32_t do_fadump_register(struct SpaprMachineState *, target_ulong);
void     trigger_fadump_boot(struct SpaprMachineState *, target_ulong);
#endif /* PPC_SPAPR_FADUMP_H */
