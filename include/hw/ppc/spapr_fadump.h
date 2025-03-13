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

/*
 * The Firmware Assisted Dump Memory structure supports a maximum of 10 sections
 * in the dump memory structure. Presently, three sections are used for
 * CPU state data, HPTE & Parameters area, while the remaining seven sections
 * can be used for boot memory regions.
 */
#define FADUMP_MAX_SECTIONS            10
#define RTAS_FADUMP_MAX_BOOT_MEM_REGS  7

typedef struct FadumpSection FadumpSection;
typedef struct FadumpSectionHeader FadumpSectionHeader;
typedef struct FadumpMemStruct FadumpMemStruct;

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

uint32_t do_fadump_register(void);
#endif /* PPC_SPAPR_FADUMP_H */
