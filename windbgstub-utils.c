/*
 * windbgstub-utils.c
 *
 * Copyright (c) 2010-2017 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "exec/windbgstub-utils.h"

#ifdef TARGET_X86_64
# define OFFSET_SELF_PCR         0x18
# define OFFSET_VERS             0x108
#else
# define OFFSET_SELF_PCR         0x1C
# define OFFSET_VERS             0x34
#endif

typedef struct KDData {
    InitedAddr KPCR;
    InitedAddr version;
} KDData;

static KDData *kd;

bool windbg_on_load(void)
{
    CPUState *cpu = qemu_get_cpu(0);
    CPUArchState *env = cpu->env_ptr;

    if (!kd) {
        kd = g_new0(KDData, 1);
    }

    if (!kd->KPCR.is_init) {

 #ifdef TARGET_X86_64
        kd->KPCR.addr = env->segs[R_GS].base;
 #else
        kd->KPCR.addr = env->segs[R_FS].base;
 #endif

        static target_ulong prev_KPCR;
        if (!kd->KPCR.addr || prev_KPCR == kd->KPCR.addr) {
            return false;
        }
        prev_KPCR = kd->KPCR.addr;

        if (kd->KPCR.addr != READ_VMEM(cpu, kd->KPCR.addr + OFFSET_SELF_PCR,
                                       target_ulong)) {
            return false;
        }

        kd->KPCR.is_init = true;
    }

    if (!kd->version.is_init && kd->KPCR.is_init) {
        kd->version.addr = READ_VMEM(cpu, kd->KPCR.addr + OFFSET_VERS,
                                     target_ulong);
        if (!kd->version.addr) {
            return false;
        }
        kd->version.is_init = true;
    }

    WINDBG_DEBUG("windbg_on_load: KPCR " FMT_ADDR, kd->KPCR.addr);
    WINDBG_DEBUG("windbg_on_load: version " FMT_ADDR, kd->version.addr);

    return true;
}

void windbg_on_exit(void)
{
    g_free(kd);
    kd = NULL;
}
