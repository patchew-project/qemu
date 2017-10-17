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
#include "exec/address-spaces.h"

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

void kd_api_read_io_space(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_WRITE_IO64 *io = &pd->m64.u.ReadWriteIo;
    CPUArchState *env = cpu->env_ptr;

    target_ulong addr = ldtul_p(&io->IoAddress);
    uint32_t value = 0;

    switch (io->DataSize) {
    case 1:
        value = address_space_ldub(&address_space_io, addr,
                                   cpu_get_mem_attrs(env), NULL);
        stl_p(&io->DataValue, value);
        break;
    case 2:
        value = address_space_lduw(&address_space_io, addr,
                                   cpu_get_mem_attrs(env), NULL);
        stl_p(&io->DataValue, value);
        break;
    case 4:
        value = address_space_ldl(&address_space_io, addr,
                                  cpu_get_mem_attrs(env), NULL);
        stl_p(&io->DataValue, value);
        break;
    default:
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
        return;
    }

    pd->m64.ReturnStatus = STATUS_SUCCESS;
}

void kd_api_write_io_space(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_WRITE_IO64 *io = &pd->m64.u.ReadWriteIo;
    CPUArchState *env = cpu->env_ptr;

    target_ulong addr = ldtul_p(&io->IoAddress);
    uint32_t value = ldl_p(&io->DataValue);

    switch (io->DataSize) {
    case 1:
        address_space_stb(&address_space_io, addr, value,
                          cpu_get_mem_attrs(env), NULL);
        break;
    case 2:
        address_space_stw(&address_space_io, addr, value,
                          cpu_get_mem_attrs(env), NULL);
        break;
    case 4:
        address_space_stl(&address_space_io, addr, value,
                          cpu_get_mem_attrs(env), NULL);
        break;
    default:
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
        return;
    }

    pd->m64.ReturnStatus = STATUS_SUCCESS;
}

void kd_api_read_physical_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_READ_MEMORY64 *mem = &pd->m64.u.ReadMemory;
    uint32_t len;
    target_ulong addr;

    len = MIN(ldl_p(&mem->TransferCount),
              PACKET_MAX_SIZE - sizeof(DBGKD_MANIPULATE_STATE64));
    addr = ldtul_p(&mem->TargetBaseAddress);

    cpu_physical_memory_rw(addr, pd->extra, len, 0);
    pd->extra_size = len;
    stl_p(&mem->ActualBytesRead, len);
}

void kd_api_write_physical_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_WRITE_MEMORY64 *mem = &pd->m64.u.WriteMemory;
    uint32_t len;
    target_ulong addr;

    len = MIN(ldl_p(&mem->TransferCount), pd->extra_size);
    addr = ldtul_p(&mem->TargetBaseAddress);

    cpu_physical_memory_rw(addr, pd->extra, len, 1);
    pd->extra_size = 0;
    stl_p(&mem->ActualBytesWritten, len);
}

void kd_api_get_version(CPUState *cpu, PacketData *pd)
{
    DBGKD_GET_VERSION64 *kdver;
    int err = cpu_memory_rw_debug(cpu, version.addr, PTR(pd->m64) + 0x10,
                                  sizeof(DBGKD_MANIPULATE_STATE64) - 0x10, 0);
    if (!err) {
        kdver = (DBGKD_GET_VERSION64 *) (PTR(pd->m64) + 0x10);

        stw_p(&kdver->MajorVersion, kdver->MajorVersion);
        stw_p(&kdver->MinorVersion, kdver->MinorVersion);
        stw_p(&kdver->Flags, kdver->Flags);
        stw_p(&kdver->MachineType, kdver->MachineType);
        stw_p(&kdver->Unused[0], kdver->Unused[0]);
        sttul_p(&kdver->KernBase, kdver->KernBase);
        sttul_p(&kdver->PsLoadedModuleList, kdver->PsLoadedModuleList);
        sttul_p(&kdver->DebuggerDataList, kdver->DebuggerDataList);
    } else {
        pd->m64.ReturnStatus = STATUS_UNSUCCESSFUL;
        WINDBG_ERROR("get_version: " FMT_ERR, err);
    }
}

void kd_api_search_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_SEARCH_MEMORY *m64c = &pd->m64.u.SearchMemory;
    int s_len = MAX(ldq_p(&m64c->SearchLength), 1);
    int p_len = MIN(ldl_p(&m64c->PatternLength), pd->extra_size);
    target_ulong addr = ldq_p(&m64c->SearchAddress);
    int size = MIN(s_len, 10);
    uint8_t mem[size + p_len];
    int i, err;

    pd->extra_size = 0;
    pd->m64.ReturnStatus = STATUS_NO_MORE_ENTRIES;

    while (s_len) {
        err = cpu_memory_rw_debug(cpu, addr, mem, size + p_len, 0);
        if (!err) {
            for (i = 0; i < size; ++i) {
                if (memcmp(mem + i, pd->extra, p_len) == 0) {
                    stl_p(&m64c->FoundAddress, addr + i);
                    pd->m64.ReturnStatus = STATUS_SUCCESS;
                    return;
                }
            }
        } else {
            WINDBG_DEBUG("search_memory: No physical page mapped: " FMT_ADDR,
                         addr);
        }
        s_len -= size;
        addr += size;
        size = MIN(s_len, 10);
    }
}

void kd_api_fill_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_FILL_MEMORY *m64c = &pd->m64.u.FillMemory;
    uint32_t len = ldl_p(&m64c->Length);
    target_ulong addr = ldq_p(&m64c->Address);
    uint16_t pattern = ldl_p(&m64c->PatternLength);
    uint16_t flags = ldl_p(&m64c->Flags);
    int err, offset = 0;

    uint8_t mem[pattern];
    memcpy(mem, pd->extra, pattern);

    pd->extra_size = 0;

    switch (flags) {
    case DBGKD_FILL_MEMORY_VIRTUAL:
        while (offset < len) {
            err = cpu_memory_rw_debug(cpu, addr + offset, mem,
                                      MIN(pattern, len - offset), 1);
            offset += pattern;
            if (err) {
                WINDBG_DEBUG("fill_memory: No physical page mapped: " FMT_ADDR,
                             addr);
            }
        }
        break;

    case DBGKD_FILL_MEMORY_PHYSICAL:
        while (offset < len) {
            cpu_physical_memory_rw(addr, mem, MIN(pattern, len - offset), 1);
            offset += pattern;
        }
        break;

    default:
        break;
    }
}

void kd_api_query_memory(CPUState *cpu, PacketData *pd)
{
    DBGKD_QUERY_MEMORY *mem = &pd->m64.u.QueryMemory;

    if (ldl_p(&mem->AddressSpace) == DBGKD_QUERY_MEMORY_VIRTUAL) {
        mem->AddressSpace = DBGKD_QUERY_MEMORY_PROCESS;
        mem->Flags = DBGKD_QUERY_MEMORY_READ |
                     DBGKD_QUERY_MEMORY_WRITE |
                     DBGKD_QUERY_MEMORY_EXECUTE;
        mem->AddressSpace = ldl_p(&mem->AddressSpace);
        mem->Flags = ldl_p(&mem->Flags);
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
