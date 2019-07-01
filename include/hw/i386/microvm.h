/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_I386_MICROVM_H
#define HW_I386_MICROVM_H

#include "qemu-common.h"
#include "exec/hwaddr.h"
#include "qemu/notify.h"

#include "hw/boards.h"

/* Microvm memory layout */
#define PVH_START_INFO        0x6000
#define MEMMAP_START          0x7000
#define BOOT_STACK_POINTER    0x8ff0
#define PML4_START            0x9000
#define PDPTE_START           0xa000
#define PDE_START             0xb000
#define KERNEL_CMDLINE_START  0x20000
#define EBDA_START            0x9fc00
#define HIMEM_START           0x100000
#define MICROVM_MAX_BELOW_4G  0xe0000000

/* Platform virtio definitions */
#define VIRTIO_MMIO_BASE      0xd0000000
#define VIRTIO_IRQ_BASE       5
#define VIRTIO_NUM_TRANSPORTS 8
#define VIRTIO_CMDLINE_MAXLEN 64

/* Machine type options */
#define MICROVM_MACHINE_LEGACY "legacy"

typedef struct {
    MachineClass parent;
    HotplugHandler *(*orig_hotplug_handler)(MachineState *machine,
                                           DeviceState *dev);
} MicrovmMachineClass;

typedef struct {
    MachineState parent;
    qemu_irq *gsi;

    /* RAM size */
    ram_addr_t below_4g_mem_size;
    ram_addr_t above_4g_mem_size;

    /* Kernel ELF entry. On reset, vCPUs RIP will be set to this */
    uint64_t elf_entry;

    /* Legacy mode based on an ISA bus. Useful for debugging */
    bool legacy;
} MicrovmMachineState;

#define TYPE_MICROVM_MACHINE   MACHINE_TYPE_NAME("microvm")
#define MICROVM_MACHINE(obj) \
    OBJECT_CHECK(MicrovmMachineState, (obj), TYPE_MICROVM_MACHINE)
#define MICROVM_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MicrovmMachineClass, obj, TYPE_MICROVM_MACHINE)
#define MICROVM_MACHINE_CLASS(class) \
    OBJECT_CLASS_CHECK(MicrovmMachineClass, class, TYPE_MICROVM_MACHINE)

#endif
