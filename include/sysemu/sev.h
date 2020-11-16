/*
 * QEMU Secure Encrypted Virutualization (SEV) support
 *
 * Copyright: Advanced Micro Devices, 2016-2018
 *
 * Authors:
 *  Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SEV_H
#define QEMU_SEV_H

#include "sysemu/kvm.h"

void *sev_guest_init(const char *id);
int sev_encrypt_data(void *handle, uint8_t *ptr, uint64_t len);
void sev_set_debug_ops_memory_region(void *handle, MemoryRegion *mr);
void sev_set_debug_ops_cpu_state(void *handle, CPUState *cpu);
hwaddr sev_cpu_get_phys_page_attrs_debug(CPUState *cs, vaddr addr,
                                         MemTxAttrs *attrs);
MemTxResult sev_address_space_read_debug(AddressSpace *as, hwaddr addr,
                                         MemTxAttrs attrs, void *ptr,
                                         hwaddr len);
MemTxResult sev_address_space_write_rom_debug(AddressSpace *as,
                                              hwaddr addr,
                                              MemTxAttrs attrs,
                                              const void *ptr,
                                              hwaddr len);
#endif
