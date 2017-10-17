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

void kd_api_read_virtual_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_MEMORY64 *mem = &pd->m64.u.ReadMemory;
    uint32_t len;
    target_ulong addr;
    int err;

    len = MIN(ldl_p(&mem->TransferCount),
                    PACKET_MAX_SIZE - sizeof(DBGKD_MANIPULATE_STATE64));
    addr = ldtul_p(&mem->TargetBaseAddress);
    err = cpu_memory_rw_debug(cpu, addr, pd->extra, len, 0);

    if (err) {
        len = 0;
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;

        WINDBG_DEBUG("read_virtual_memory: No physical page mapped: " FMT_ADDR,
                     (target_ulong) mem->TargetBaseAddress);
    }

    pd->extra_size = len;
    stl_p(&mem->ActualBytesRead, len);
}

void kd_api_write_virtual_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_WRITE_MEMORY64 *mem = &pd->m64.u.WriteMemory;
    uint32_t len;
    target_ulong addr;
    int err;

    len = MIN(ldl_p(&mem->TransferCount), pd->extra_size);
    addr = ldtul_p(&mem->TargetBaseAddress);
    err = cpu_memory_rw_debug(cpu, addr, pd->extra, len, 1);

    if (err) {
        len = 0;
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;

        WINDBG_DEBUG("read_write_memory: No physical page mapped: " FMT_ADDR,
                     (target_ulong) mem->TargetBaseAddress);
    }

    pd->extra_size = 0;
    stl_p(&mem->ActualBytesWritten, len);
}

void kd_api_unsupported(CPUState *cpu, PacketData *pd)
{
    WINDBG_ERROR("Caught unimplemented api %s",
                 KD_API_NAME(pd->m64.ApiNumber));
    pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    pd->extra_size = 0;

    exit(1);
}
