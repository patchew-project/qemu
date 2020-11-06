/*
 * LEFI (a UEFI-like interface for BIOS-Kernel boot parameters) helpers
 *
 * Copyright (c) 2018-2020 Huacai Chen (chenhc@lemote.com)
 * Copyright (c) 2018-2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/mips/loongson3_bootp.h"

#define LOONGSON3_CORE_PER_NODE 4

static struct efi_cpuinfo_loongson *init_cpu_info(void *g_cpuinfo, uint64_t cpu_freq)
{
    struct efi_cpuinfo_loongson *c = g_cpuinfo;

    stl_le_p(&c->cputype, Loongson_3A);
    stl_le_p(&c->processor_id, MIPS_CPU(first_cpu)->env.CP0_PRid);
    if (cpu_freq > UINT_MAX) {
        stl_le_p(&c->cpu_clock_freq, UINT_MAX);
    } else {
        stl_le_p(&c->cpu_clock_freq, cpu_freq);
    }

    stw_le_p(&c->cpu_startup_core_id, 0);
    stl_le_p(&c->nr_cpus, current_machine->smp.cpus);
    stl_le_p(&c->total_node, DIV_ROUND_UP(current_machine->smp.cpus,
                                          LOONGSON3_CORE_PER_NODE));

    return c;
}

static struct efi_memory_map_loongson *init_memory_map(void *g_map, uint64_t ram_size)
{
    struct efi_memory_map_loongson *emap = g_map;

    stl_le_p(&emap->nr_map, 2);
    stl_le_p(&emap->mem_freq, 300000000);

    stl_le_p(&emap->map[0].node_id, 0);
    stl_le_p(&emap->map[0].mem_type, 1);
    stq_le_p(&emap->map[0].mem_start, 0x0);
    stl_le_p(&emap->map[0].mem_size, 240);

    stl_le_p(&emap->map[1].node_id, 0);
    stl_le_p(&emap->map[1].mem_type, 2);
    stq_le_p(&emap->map[1].mem_start, 0x90000000);
    stl_le_p(&emap->map[1].mem_size, (ram_size / MiB) - 256);

    return emap;
}

static struct system_loongson *init_system_loongson(void *g_system)
{
    struct system_loongson *s = g_system;

    stl_le_p(&s->ccnuma_smp, 0);
    stl_le_p(&s->sing_double_channel, 1);
    stl_le_p(&s->nr_uarts, 1);
    stl_le_p(&s->uarts[0].iotype, 2);
    stl_le_p(&s->uarts[0].int_offset, 2);
    stl_le_p(&s->uarts[0].uartclk, 25000000); /* Random value */
    stq_le_p(&s->uarts[0].uart_base, virt_memmap[VIRT_UART].base);

    return s;
}

static struct irq_source_routing_table *init_irq_source(void *g_irq_source)
{
    struct irq_source_routing_table *irq_info = g_irq_source;

    stl_le_p(&irq_info->node_id, 0);
    stl_le_p(&irq_info->PIC_type, 0);
    stw_le_p(&irq_info->dma_mask_bits, 64);
    stq_le_p(&irq_info->pci_mem_start_addr, virt_memmap[VIRT_PCIE_MMIO].base);
    stq_le_p(&irq_info->pci_mem_end_addr, virt_memmap[VIRT_PCIE_MMIO].base +
                                          virt_memmap[VIRT_PCIE_MMIO].size - 1);
    stq_le_p(&irq_info->pci_io_start_addr, virt_memmap[VIRT_PCIE_PIO].base);

    return irq_info;
}

static struct interface_info *init_interface_info(void *g_interface)
{
    struct interface_info *interface = g_interface;

    stw_le_p(&interface->vers, 0x01);
    strpadcpy(interface->description, 64, "UEFI_Version_v1.0", '\0');

    return interface;
}

static struct board_devices *board_devices_info(void *g_board)
{
    struct board_devices *bd = g_board;

    strpadcpy(bd->name, 64, "Loongson-3A-VIRT-1w-V1.00-demo", '\0');

    return bd;
}

static struct loongson_special_attribute *init_special_info(void *g_special)
{
    struct loongson_special_attribute *special = g_special;

    strpadcpy(special->special_name, 64, "2018-04-01", '\0');

    return special;
}

void init_loongson_params(struct loongson_params *lp, void *p,
                          uint64_t cpu_freq, uint64_t ram_size)
{
    stq_le_p(&lp->cpu_offset,
              (uintptr_t)init_cpu_info(p, cpu_freq) - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct efi_cpuinfo_loongson), 64);

    stq_le_p(&lp->memory_offset,
              (uintptr_t)init_memory_map(p, ram_size) - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct efi_memory_map_loongson), 64);

    stq_le_p(&lp->system_offset,
              (uintptr_t)init_system_loongson(p) - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct system_loongson), 64);

    stq_le_p(&lp->irq_offset,
              (uintptr_t)init_irq_source(p) - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct irq_source_routing_table), 64);

    stq_le_p(&lp->interface_offset,
              (uintptr_t)init_interface_info(p) - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct interface_info), 64);

    stq_le_p(&lp->boarddev_table_offset,
              (uintptr_t)board_devices_info(p) - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct board_devices), 64);

    stq_le_p(&lp->special_offset,
              (uintptr_t)init_special_info(p) - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct loongson_special_attribute), 64);
}

void init_reset_system(struct efi_reset_system_t *reset)
{
    stq_le_p(&reset->Shutdown, 0xffffffffbfc000a8);
    stq_le_p(&reset->ResetCold, 0xffffffffbfc00080);
    stq_le_p(&reset->ResetWarm, 0xffffffffbfc00080);
}
