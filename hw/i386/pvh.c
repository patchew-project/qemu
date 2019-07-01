/*
 * PVH Boot Helper
 *
 * Copyright (C) 2019 Oracle
 * Copyright (C) 2019 Red Hat, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "hw/loader.h"
#include "cpu.h"
#include "elf.h"
#include "pvh.h"

static size_t pvh_start_addr = 0;

size_t pvh_get_start_addr(void)
{
    return pvh_start_addr;
}

/*
 * The entry point into the kernel for PVH boot is different from
 * the native entry point.  The PVH entry is defined by the x86/HVM
 * direct boot ABI and is available in an ELFNOTE in the kernel binary.
 *
 * This function is passed to load_elf() when it is called from
 * load_elfboot() which then additionally checks for an ELF Note of
 * type XEN_ELFNOTE_PHYS32_ENTRY and passes it to this function to
 * parse the PVH entry address from the ELF Note.
 *
 * Due to trickery in elf_opts.h, load_elf() is actually available as
 * load_elf32() or load_elf64() and this routine needs to be able
 * to deal with being called as 32 or 64 bit.
 *
 * The address of the PVH entry point is saved to the 'pvh_start_addr'
 * global variable.  (although the entry point is 32-bit, the kernel
 * binary can be either 32-bit or 64-bit).
 */

static uint64_t read_pvh_start_addr(void *arg1, void *arg2, bool is64)
{
    size_t *elf_note_data_addr;

    /* Check if ELF Note header passed in is valid */
    if (arg1 == NULL) {
        return 0;
    }

    if (is64) {
        struct elf64_note *nhdr64 = (struct elf64_note *)arg1;
        uint64_t nhdr_size64 = sizeof(struct elf64_note);
        uint64_t phdr_align = *(uint64_t *)arg2;
        uint64_t nhdr_namesz = nhdr64->n_namesz;

        elf_note_data_addr =
            ((void *)nhdr64) + nhdr_size64 +
            QEMU_ALIGN_UP(nhdr_namesz, phdr_align);
    } else {
        struct elf32_note *nhdr32 = (struct elf32_note *)arg1;
        uint32_t nhdr_size32 = sizeof(struct elf32_note);
        uint32_t phdr_align = *(uint32_t *)arg2;
        uint32_t nhdr_namesz = nhdr32->n_namesz;

        elf_note_data_addr =
            ((void *)nhdr32) + nhdr_size32 +
            QEMU_ALIGN_UP(nhdr_namesz, phdr_align);
    }

    pvh_start_addr = *elf_note_data_addr;

    return pvh_start_addr;
}

bool pvh_load_elfboot(const char *kernel_filename,
                      uint32_t *mh_load_addr,
                      uint32_t *elf_kernel_size)
{
    uint64_t elf_entry;
    uint64_t elf_low, elf_high;
    int kernel_size;
    uint64_t elf_note_type = XEN_ELFNOTE_PHYS32_ENTRY;

    kernel_size = load_elf(kernel_filename, read_pvh_start_addr,
                           NULL, &elf_note_type, &elf_entry,
                           &elf_low, &elf_high, 0, I386_ELF_MACHINE,
                           0, 0);

    if (kernel_size < 0) {
        error_report("Error while loading elf kernel");
        return false;
    }

    if (pvh_start_addr == 0) {
        error_report("Error loading uncompressed kernel without PVH ELF Note");
        return false;
    }

    if (mh_load_addr) {
        *mh_load_addr = elf_low;
    }

    if (elf_kernel_size) {
        *elf_kernel_size = elf_high - elf_low;
    }

    return true;
}
