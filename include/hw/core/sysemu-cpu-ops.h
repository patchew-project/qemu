/*
 * CPU operations specific to system emulation
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSEMU_CPU_OPS_H
#define SYSEMU_CPU_OPS_H

#include "hw/core/cpu.h"

struct CPUWatchpoint {
    vaddr vaddr;
    vaddr len;
    vaddr hitaddr;
    MemTxAttrs hitattrs;
    int flags; /* BP_* */
    QTAILQ_ENTRY(CPUWatchpoint) entry;
};

/*
 * struct SysemuCPUOps: System operations specific to a CPU class
 */
typedef struct SysemuCPUOps {
    /**
     * @get_memory_mapping: Callback for obtaining the memory mappings.
     */
    void (*get_memory_mapping)(CPUState *cpu, MemoryMappingList *list,
                               Error **errp);
    /**
     * @get_paging_enabled: Callback for inquiring whether paging is enabled.
     */
    bool (*get_paging_enabled)(const CPUState *cpu);
    /**
     * @get_phys_page_debug: Callback for obtaining a physical address.
     */
    hwaddr (*get_phys_page_debug)(CPUState *cpu, vaddr addr);
    /**
     * @get_phys_page_attrs_debug: Callback for obtaining a physical address
     *       and the associated memory transaction attributes to use for the
     *       access.
     * CPUs which use memory transaction attributes should implement this
     * instead of get_phys_page_debug.
     */
    hwaddr (*get_phys_page_attrs_debug)(CPUState *cpu, vaddr addr,
                                        MemTxAttrs *attrs);
    /**
     * @asidx_from_attrs: Callback to return the CPU AddressSpace to use for
     *       a memory access with the specified memory transaction attributes.
     */
    int (*asidx_from_attrs)(CPUState *cpu, MemTxAttrs attrs);
    /**
     * @get_crash_info: Callback for reporting guest crash information in
     * GUEST_PANICKED events.
     */
    GuestPanicInformation* (*get_crash_info)(CPUState *cpu);
    /**
     * @write_elf32_note: Callback for writing a CPU-specific ELF note to a
     * 32-bit VM coredump.
     */
    int (*write_elf32_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, void *opaque);
    /**
     * @write_elf64_note: Callback for writing a CPU-specific ELF note to a
     * 64-bit VM coredump.
     */
    int (*write_elf64_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, void *opaque);
    /**
     * @write_elf32_qemunote: Callback for writing a CPU- and QEMU-specific ELF
     * note to a 32-bit VM coredump.
     */
    int (*write_elf32_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                void *opaque);
    /**
     * @write_elf64_qemunote: Callback for writing a CPU- and QEMU-specific ELF
     * note to a 64-bit VM coredump.
     */
    int (*write_elf64_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                void *opaque);
    /**
     * @virtio_is_big_endian: Callback to return %true if a CPU which supports
     *       runtime configurable endianness is currently big-endian.
     * Non-configurable CPUs can use the default implementation of this method.
     * This method should not be used by any callers other than the pre-1.0
     * virtio devices.
     */
    bool (*virtio_is_big_endian)(CPUState *cpu);
    /**
     * @vmsd: State description for migration.
     */
    const VMStateDescription *vmsd;
} SysemuCPUOps;

/**
 * cpu_paging_enabled:
 * @cpu: The CPU whose state is to be inspected.
 *
 * Returns: %true if paging is enabled, %false otherwise.
 */
bool cpu_paging_enabled(const CPUState *cpu);

/**
 * cpu_get_memory_mapping:
 * @cpu: The CPU whose memory mappings are to be obtained.
 * @list: Where to write the memory mappings to.
 * @errp: Pointer for reporting an #Error.
 */
void cpu_get_memory_mapping(CPUState *cpu, MemoryMappingList *list,
                            Error **errp);

/**
 * cpu_write_elf64_note:
 * @f: pointer to a function that writes memory to a file
 * @cpu: The CPU whose memory is to be dumped
 * @cpuid: ID number of the CPU
 * @opaque: pointer to the CPUState struct
 */
int cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque);

/**
 * cpu_write_elf64_qemunote:
 * @f: pointer to a function that writes memory to a file
 * @cpu: The CPU whose memory is to be dumped
 * @cpuid: ID number of the CPU
 * @opaque: pointer to the CPUState struct
 */
int cpu_write_elf64_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque);

/**
 * cpu_write_elf32_note:
 * @f: pointer to a function that writes memory to a file
 * @cpu: The CPU whose memory is to be dumped
 * @cpuid: ID number of the CPU
 * @opaque: pointer to the CPUState struct
 */
int cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque);

/**
 * cpu_write_elf32_qemunote:
 * @f: pointer to a function that writes memory to a file
 * @cpu: The CPU whose memory is to be dumped
 * @cpuid: ID number of the CPU
 * @opaque: pointer to the CPUState struct
 */
int cpu_write_elf32_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque);

/**
 * cpu_get_crash_info:
 * @cpu: The CPU to get crash information for
 *
 * Gets the previously saved crash information.
 * Caller is responsible for freeing the data.
 */
GuestPanicInformation *cpu_get_crash_info(CPUState *cpu);

/**
 * cpu_get_phys_page_attrs_debug:
 * @cpu: The CPU to obtain the physical page address for.
 * @addr: The virtual address.
 * @attrs: Updated on return with the memory transaction attributes to use
 *         for this access.
 *
 * Obtains the physical page corresponding to a virtual one, together
 * with the corresponding memory transaction attributes to use for the access.
 * Use it only for debugging because no protection checks are done.
 *
 * Returns: Corresponding physical page address or -1 if no page found.
 */
hwaddr cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                     MemTxAttrs *attrs);

/**
 * cpu_get_phys_page_debug:
 * @cpu: The CPU to obtain the physical page address for.
 * @addr: The virtual address.
 *
 * Obtains the physical page corresponding to a virtual one.
 * Use it only for debugging because no protection checks are done.
 *
 * Returns: Corresponding physical page address or -1 if no page found.
 */
hwaddr cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

/**
 * cpu_asidx_from_attrs:
 * @cpu: CPU
 * @attrs: memory transaction attributes
 *
 * Returns the address space index specifying the CPU AddressSpace
 * to use for a memory access with the given transaction attributes.
 */
int cpu_asidx_from_attrs(CPUState *cpu, MemTxAttrs attrs);

/**
 * cpu_virtio_is_big_endian:
 * @cpu: CPU

 * Returns %true if a CPU which supports runtime configurable endianness
 * is currently big-endian.
 */
bool cpu_virtio_is_big_endian(CPUState *cpu);

int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          int flags, CPUWatchpoint **watchpoint);
int cpu_watchpoint_remove(CPUState *cpu, vaddr addr,
                          vaddr len, int flags);
void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *watchpoint);
void cpu_watchpoint_remove_all(CPUState *cpu, int mask);

/**
 * cpu_check_watchpoint:
 * @cpu: cpu context
 * @addr: guest virtual address
 * @len: access length
 * @attrs: memory access attributes
 * @flags: watchpoint access type
 * @ra: unwind return address
 *
 * Check for a watchpoint hit in [addr, addr+len) of the type
 * specified by @flags.  Exit via exception with a hit.
 */
void cpu_check_watchpoint(CPUState *cpu, vaddr addr, vaddr len,
                          MemTxAttrs attrs, int flags, uintptr_t ra);

/**
 * cpu_watchpoint_address_matches:
 * @cpu: cpu context
 * @addr: guest virtual address
 * @len: access length
 *
 * Return the watchpoint flags that apply to [addr, addr+len).
 * If no watchpoint is registered for the range, the result is 0.
 */
int cpu_watchpoint_address_matches(CPUState *cpu, vaddr addr, vaddr len);

#endif /* SYSEMU_CPU_OPS_H */
