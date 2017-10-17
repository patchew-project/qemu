/*
 * windbgstub.c
 *
 * Copyright (c) 2010-2017 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "exec/windbgstub-utils.h"

#ifdef TARGET_X86_64
# define OFFSET_SELF_PCR         0x18
# define OFFSET_VERS             0x108
#else
# define OFFSET_SELF_PCR         0x1C
# define OFFSET_VERS             0x34
#endif

bool windbg_on_load(void)
{
    CPUState *cpu = qemu_get_cpu(0);
    CPUArchState *env = cpu->env_ptr;
    InitedAddr *KPCR = windbg_get_KPCR();
    InitedAddr *version = windbg_get_version();

    if (!KPCR->is_init) {

 #ifdef TARGET_X86_64
        KPCR->addr = env->segs[R_GS].base;
 #else
        KPCR->addr = env->segs[R_FS].base;
 #endif

        static target_ulong prev_KPCR;
        if (!KPCR->addr || prev_KPCR == KPCR->addr) {
            return false;
        }
        prev_KPCR = KPCR->addr;

        if (KPCR->addr != READ_VMEM(cpu, KPCR->addr + OFFSET_SELF_PCR,
                                    target_ulong)) {
            return false;
        }

        KPCR->is_init = true;
    }

    if (!version->is_init && KPCR->is_init) {
        version->addr = READ_VMEM(cpu, KPCR->addr + OFFSET_VERS,
                                  target_ulong);
        if (!version->addr) {
            return false;
        }
        version->is_init = true;
    }

    WINDBG_DEBUG("windbg_on_load: KPCR " FMT_ADDR, KPCR->addr);
    WINDBG_DEBUG("windbg_on_load: version " FMT_ADDR, version->addr);

    return true;
}
