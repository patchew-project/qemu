/*
 * Emulation of MPIPL (Memory Preserving Initial Program Load), aka fadump
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PNV_MPIPL_H
#define PNV_MPIPL_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"

#include <assert.h>

typedef struct MdstTableEntry MdstTableEntry;
typedef struct MdrtTableEntry MdrtTableEntry;
typedef struct MpiplPreservedState MpiplPreservedState;

/* Following offsets are copied from skiboot source code */
/* Use 768 bytes for SPIRAH */
#define SPIRAH_OFF      0x00010000
#define SPIRAH_SIZE     0x300

/* Use 256 bytes for processor dump area */
#define PROC_DUMP_AREA_OFF  (SPIRAH_OFF + SPIRAH_SIZE)
#define PROC_DUMP_AREA_SIZE 0x100

#define PROCIN_OFF      (PROC_DUMP_AREA_OFF + PROC_DUMP_AREA_SIZE)
#define PROCIN_SIZE     0x800

/* Offsets of MDST and MDDT tables from skiboot base */
#define MDST_TABLE_OFF      (PROCIN_OFF + PROCIN_SIZE)
#define MDST_TABLE_SIZE     0x400

#define MDDT_TABLE_OFF      (MDST_TABLE_OFF + MDST_TABLE_SIZE)
#define MDDT_TABLE_SIZE     0x400
/*
 * Offset of the dump result table MDRT. Hostboot will write to this
 * memory after moving memory content from source to destination memory.
 */
#define MDRT_TABLE_OFF         0x01c00000
#define MDRT_TABLE_SIZE        0x00008000

/* HRMOR_BIT copied from skiboot */
#define HRMOR_BIT (1ul << 63)

#define __packed             __attribute__((packed))

/*
 * Memory Dump Source Table (MDST)
 *
 * Format of this table is same as Memory Dump Source Table defined in HDAT
 */
struct MdstTableEntry {
    uint64_t  addr;
    uint8_t data_region;
    uint8_t dump_type;
    uint16_t  reserved;
    uint32_t  size;
} __packed;

/* Memory dump destination table (MDDT) has same structure as MDST */
typedef MdstTableEntry MddtTableEntry;

/*
 * Memory dump result table (MDRT)
 *
 * List of the memory ranges that have been included in the dump. This table is
 * filled by hostboot and passed to OPAL on second boot. OPAL/payload will use
 * this table to extract the dump.
 *
 * Note: This structure differs from HDAT, but matches the structure
 * skiboot uses
 */
struct MdrtTableEntry {
    uint64_t  src_addr;
    uint64_t  dest_addr;
    uint8_t data_region;
    uint8_t dump_type;  /* unused */
    uint16_t  reserved;   /* unused */
    uint32_t  size;
    uint64_t  padding;    /* unused */
} __packed;

/* Maximum length of mdst/mddt/mdrt tables */
#define MDST_MAX_ENTRIES    (MDST_TABLE_SIZE / sizeof(MdstTableEntry))
#define MDDT_MAX_ENTRIES    (MDDT_TABLE_SIZE / sizeof(MddtTableEntry))
#define MDRT_MAX_ENTRIES    (MDRT_TABLE_SIZE / sizeof(MdrtTableEntry))

static_assert(MDST_MAX_ENTRIES == MDDT_MAX_ENTRIES,
        "Maximum entries in MDDT must match MDST");
static_assert(MDRT_MAX_ENTRIES >= MDST_MAX_ENTRIES,
        "MDRT should support atleast having number of entries as in MDST");

/* Preserved state to be saved in PnvMachineState */
struct MpiplPreservedState {
    /* skiboot_base will be valid only after OPAL sends relocated base to SBE */
    hwaddr     skiboot_base;
    bool       is_next_boot_mpipl;

    MdrtTableEntry *mdrt_table;
    uint32_t num_mdrt_entries;
};

#endif
