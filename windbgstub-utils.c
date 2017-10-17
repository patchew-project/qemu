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

static InitedAddr KPCR;
static InitedAddr version;

InitedAddr *windbg_get_KPCR(void)
{
    return &KPCR;
}

InitedAddr *windbg_get_version(void)
{
    return &version;
}

void kd_api_unsupported(CPUState *cpu, PacketData *pd)
{
    WINDBG_ERROR("Caught unimplemented api %s",
                 KD_API_NAME(pd->m64.ApiNumber));
    pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    pd->extra_size = 0;

    exit(1);
}
