/*
 * QEMU dump
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef DUMP_ARCH_H
#define DUMP_ARCH_H

typedef struct ArchDumpInfo {
    int d_machine;           /* Architecture */
    int d_endian;            /* ELFDATA2LSB or ELFDATA2MSB */
    int d_class;             /* ELFCLASS32 or ELFCLASS64 */
    uint32_t page_size;      /* The target's page size. If it's variable and
                              * unknown, then this should be the maximum. */
    uint64_t phys_base;      /* The target's physmem base. */
    void (*arch_sections_add_fn)(void *opaque);
    uint64_t (*arch_sections_write_hdr_fn)(void *opaque, uint8_t *buff);
    void (*arch_sections_write_fn)(void *opaque, uint8_t *buff);
} ArchDumpInfo;

struct GuestPhysBlockList; /* memory_mapping.h */
int cpu_get_dump_info(ArchDumpInfo *info,
                      const struct GuestPhysBlockList *guest_phys_blocks);
ssize_t cpu_get_note_size(int class, int machine, int nr_cpus);

static inline void dump_arch_sections_add(ArchDumpInfo *info, void *opaque)
{
    if (info->arch_sections_add_fn) {
        info->arch_sections_add_fn(opaque);
    }
}

static inline uint64_t dump_arch_sections_write_hdr(ArchDumpInfo *info,
                                                void *opaque, uint8_t *buff)
{
    if (info->arch_sections_write_hdr_fn) {
        return info->arch_sections_write_hdr_fn(opaque, buff);
    }
    return 0;
}

static inline void dump_arch_sections_write(ArchDumpInfo *info, void *opaque,
                                            uint8_t *buff)
{
    if (info->arch_sections_write_fn) {
        info->arch_sections_write_fn(opaque, buff);
    }
}

#endif
