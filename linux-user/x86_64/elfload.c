/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu.h"
#include "loader.h"


abi_ulong get_elf_hwcap(CPUState *cs)
{
    return cpu_env(cs)->features[FEAT_1_EDX];
}

const char *get_elf_platform(CPUState *cs)
{
    return "x86_64";
}

bool init_guest_commpage(void)
{
    /*
     * The vsyscall page is at a high negative address aka kernel space,
     * which means that we cannot actually allocate it with target_mmap.
     * We still should be able to use page_set_flags, unless the user
     * has specified -R reserved_va, which would trigger an assert().
     */
    if (reserved_va != 0 &&
        TARGET_VSYSCALL_PAGE + TARGET_PAGE_SIZE - 1 > reserved_va) {
        error_report("Cannot allocate vsyscall page");
        exit(EXIT_FAILURE);
    }
    page_set_flags(TARGET_VSYSCALL_PAGE,
                   TARGET_VSYSCALL_PAGE | ~TARGET_PAGE_MASK,
                   PAGE_EXEC | PAGE_VALID);
    return true;
}
