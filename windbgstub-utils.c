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
#include "sysemu/sysemu.h"

static InitedAddr KPCR;
static InitedAddr version;

static InitedAddr bps[KD_BREAKPOINT_MAX];

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

void kd_api_write_breakpoint(CPUState *cpu, PacketData *pd)
{
    DBGKD_WRITE_BREAKPOINT64 *m64c = &pd->m64.u.WriteBreakPoint;
    target_ulong addr;
    int i, err = 0;

    addr = ldtul_p(&m64c->BreakPointAddress);

    for (i = 0; i < KD_BREAKPOINT_MAX; ++i) {
        if (!bps[i].is_init) {
            err = cpu_breakpoint_insert(cpu, addr, BP_GDB, NULL);
            if (!err) {
                bps[i].addr = addr;
                bps[i].is_init = true;
                WINDBG_DEBUG("write_breakpoint: " FMT_ADDR, addr);
                break;
            } else {
                WINDBG_ERROR("write_breakpoint: " FMT_ADDR ", " FMT_ERR,
                             addr, err);
                pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
                return;
            }
        } else if (addr == bps[i].addr) {
            break;
        }
    }

    if (!err) {
        stl_p(&m64c->BreakPointHandle, i + 1);
        pd->m64.ReturnStatus = STATUS_SUCCESS;
    } else {
        WINDBG_ERROR("write_breakpoint: All breakpoints occupied");
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    }
}

void kd_api_restore_breakpoint(CPUState *cpu, PacketData *pd)
{
    DBGKD_RESTORE_BREAKPOINT *m64c = &pd->m64.u.RestoreBreakPoint;
    uint8_t index;
    int err = -1;

    index = ldtul_p(&m64c->BreakPointHandle) - 1;

    if (bps[index].is_init) {
        err = cpu_breakpoint_remove(cpu, bps[index].addr, BP_GDB);
        if (!err) {
            WINDBG_DEBUG("restore_breakpoint: " FMT_ADDR ", index(%d)",
                         bps[index].addr, index);
        } else {
            WINDBG_ERROR("restore_breakpoint: " FMT_ADDR ", index(%d), "
                         FMT_ERR, bps[index].addr, index, err);
        }
        bps[index].is_init = false;
        pd->m64.ReturnStatus = STATUS_SUCCESS;
    } else {
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    }
}

void kd_api_continue(CPUState *cpu, PacketData *pd)
{
    uint32_t status = ldl_p(&pd->m64.u.Continue2.ContinueStatus);
    uint32_t trace = ldl_p(&pd->m64.u.Continue2.ControlSet.TraceFlag);
    int ssFlag = trace ? SSTEP_ENABLE | SSTEP_NOIRQ | SSTEP_NOTIMER : 0;

    if (NT_SUCCESS(status)) {
        cpu_single_step(cpu, ssFlag);
        if (!runstate_needs_reset()) {
            vm_start();
        }
    }
}

void kd_api_unsupported(CPUState *cpu, PacketData *pd)
{
    WINDBG_ERROR("Caught unimplemented api %s",
                 KD_API_NAME(pd->m64.ApiNumber));
    pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
    pd->extra_size = 0;

    exit(1);
}
