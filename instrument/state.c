/*
 * Interface for accessing guest state.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu/compiler.h"
#include "cpu.h"
#include "exec/cpu-all.h"
#include "instrument/control.h"
#include "instrument/error.h"
#include "instrument/qemu-instr/state.h"


SYM_PUBLIC bool qi_mem_read_virt(QICPU vcpu, uint64_t vaddr,
                                 size_t size, void *buf)
{
    CPUState *vcpu_ = instr_cpu_from_qicpu(vcpu);
    ERROR_IF_RET(!instr_get_state(), false, "called outside instrumentation");
    ERROR_IF_RET(!vcpu_, false, "invalid QICPU");
    return cpu_memory_rw_debug(vcpu_, vaddr, buf, size, 0) == 0;
}

SYM_PUBLIC bool qi_mem_write_virt(QICPU vcpu, uint64_t vaddr,
                                  size_t size, void *buf)
{
    CPUState *vcpu_ = instr_cpu_from_qicpu(vcpu);
    ERROR_IF_RET(!instr_get_state(), false, "called outside instrumentation");
    ERROR_IF_RET(!vcpu_, false, "invalid QICPU");
    return cpu_memory_rw_debug(vcpu_, vaddr, buf, size, 1) == 0;
}

SYM_PUBLIC bool qi_mem_virt_to_phys(QICPU vcpu, uint64_t vaddr, uint64_t *paddr)
{
    CPUState *vcpu_ = instr_cpu_from_qicpu(vcpu);
    ERROR_IF_RET(!instr_get_state(), false, "called outside instrumentation");
    ERROR_IF_RET(!vcpu_, false, "invalid QICPU");

#if defined(CONFIG_USER_ONLY)
    *paddr = vaddr;
    return true;
#else
    *paddr = cpu_get_phys_page_debug(vcpu_, vaddr);
    return *paddr != -1;
#endif
}

SYM_PUBLIC bool qi_mem_read_phys(uint64_t paddr, size_t size, void *buf)
{
    ERROR_IF_RET(!instr_get_state(), false, "called outside instrumentation");
#if defined(CONFIG_USER_ONLY)
    return cpu_memory_rw_debug(NULL, paddr, buf, size, 0) == 0;
#else
    cpu_physical_memory_read(paddr, buf, size);
    return true;
#endif
}

SYM_PUBLIC bool qi_mem_write_phys(uint64_t paddr, size_t size, void *buf)
{
    ERROR_IF_RET(!instr_get_state(), false, "called outside instrumentation");
#if defined(CONFIG_USER_ONLY)
    return cpu_memory_rw_debug(NULL, paddr, buf, size, 1) == 0;
#else
    cpu_physical_memory_write(paddr, buf, size);
    return true;
#endif
}
