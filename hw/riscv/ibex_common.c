/*
 * QEMU RISC-V Helpers for LowRISC Ibex Demo System & OpenTitan EarlGrey
 *
 * Copyright (c) 2022-2023 Rivos, Inc.
 *
 * Author(s):
 *  Emmanuel Blot <eblot@rivosinc.com>
 *  Loïc Lefort <loic@rivosinc.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "cpu.h"
#include "disas/disas.h"
#include "elf.h"
#include "exec/hwaddr.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/misc/unimp.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/ibex_common.h"
#include "hw/sysbus.h"
#include "monitor/monitor.h"


static void ibex_mmio_map_device(SysBusDevice *dev, MemoryRegion *mr,
                                 unsigned nr, hwaddr addr)
{
    assert(nr < dev->num_mmio);
    assert(dev->mmio[nr].addr == (hwaddr)-1);
    dev->mmio[nr].addr = addr;
    memory_region_add_subregion(mr, addr, dev->mmio[nr].memory);
}

DeviceState **ibex_create_devices(const IbexDeviceDef *defs, unsigned count,
                                  DeviceState *parent)
{
    DeviceState **devices = g_new0(DeviceState *, count);
    unsigned unimp_count = 0;
    for (unsigned idx = 0; idx < count; idx++) {
        const IbexDeviceDef *def = &defs[idx];
        assert(def->type);
        devices[idx] = qdev_new(def->type);

        char *name;
        if (!strcmp(def->type, TYPE_UNIMPLEMENTED_DEVICE)) {
            if (def->name) {
                name = g_strdup_printf("%s[%u]", def->name, def->instance);
            } else {
                name = g_strdup_printf(TYPE_UNIMPLEMENTED_DEVICE "[%u]",
                                       unimp_count);
            }
            unimp_count += 1u;
        } else {
            name = g_strdup_printf("%s[%u]", def->type, def->instance);
        }
        object_property_add_child(OBJECT(parent), name, OBJECT(devices[idx]));
        g_free(name);
    }
    return devices;
}

void ibex_link_devices(DeviceState **devices, const IbexDeviceDef *defs,
                       unsigned count)
{
    /* Link devices */
    for (unsigned idx = 0; idx < count; idx++) {
        DeviceState *dev = devices[idx];
        const IbexDeviceLinkDef *link = defs[idx].link;
        if (link) {
            while (link->propname) {
                DeviceState *target = devices[link->index];
                (void)object_property_set_link(OBJECT(dev), link->propname,
                                               OBJECT(target), &error_fatal);
                link++;
            }
        }
    }
}

void ibex_define_device_props(DeviceState **devices, const IbexDeviceDef *defs,
                              unsigned count)
{
    for (unsigned idx = 0; idx < count; idx++) {
        DeviceState *dev = devices[idx];
        const IbexDevicePropDef *prop = defs[idx].prop;
        if (prop) {
            while (prop->propname) {
                switch (prop->type) {
                case IBEX_PROP_TYPE_BOOL:
                    object_property_set_bool(OBJECT(dev), prop->propname,
                                             prop->b, &error_fatal);
                    break;
                case IBEX_PROP_TYPE_INT:
                    object_property_set_int(OBJECT(dev), prop->propname,
                                            prop->i, &error_fatal);
                    break;
                case IBEX_PROP_TYPE_UINT:
                    object_property_set_int(OBJECT(dev), prop->propname,
                                            prop->u, &error_fatal);
                    break;
                case IBEX_PROP_TYPE_STR:
                    object_property_set_str(OBJECT(dev), prop->propname,
                                            prop->s, &error_fatal);
                    break;
                default:
                    g_assert_not_reached();
                    break;
                }
                prop++;
            }
        }
    }
}

void ibex_realize_system_devices(DeviceState **devices,
                                 const IbexDeviceDef *defs, unsigned count)
{
    BusState *bus = sysbus_get_default();

    ibex_realize_devices(devices, bus, defs, count);

    MemoryRegion *mrs[] = { get_system_memory(), NULL, NULL, NULL };

    ibex_map_devices(devices, mrs, defs, count);
}

void ibex_realize_devices(DeviceState **devices, BusState *bus,
                          const IbexDeviceDef *defs, unsigned count)
{
    /* Realize devices */
    for (unsigned idx = 0; idx < count; idx++) {
        DeviceState *dev = devices[idx];
        const IbexDeviceDef *def = &defs[idx];

        if (def->cfg) {
            def->cfg(dev, def, DEVICE(OBJECT(dev)->parent));
        }

        if (def->memmap) {
            SysBusDevice *busdev =
                (SysBusDevice *)object_dynamic_cast(OBJECT(dev),
                                                    TYPE_SYS_BUS_DEVICE);
            if (!busdev) {
                /* non-sysbus devices are not supported for now */
                g_assert_not_reached();
            }

            qdev_realize_and_unref(DEVICE(busdev), bus, &error_fatal);
        } else {
            /* device is not connected to a bus */
            qdev_realize_and_unref(dev, NULL, &error_fatal);
        }
    }
}

void ibex_map_devices(DeviceState **devices, MemoryRegion **mrs,
                      const IbexDeviceDef *defs, unsigned count)
{
    for (unsigned idx = 0; idx < count; idx++) {
        DeviceState *dev = devices[idx];
        const IbexDeviceDef *def = &defs[idx];

        if (def->memmap) {
            SysBusDevice *busdev =
                (SysBusDevice *)object_dynamic_cast(OBJECT(dev),
                                                    TYPE_SYS_BUS_DEVICE);
            if (busdev) {
                const MemMapEntry *memmap = def->memmap;
                unsigned mem = 0;
                while (memmap->size) {
                    unsigned region = IBEX_MEMMAP_GET_REGIDX(memmap->base);
                    MemoryRegion *mr = mrs[region];
                    if (mr) {
                        ibex_mmio_map_device(busdev, mr, mem,
                                             IBEX_MEMMAP_GET_ADDRESS(
                                                 memmap->base));
                    }
                    mem++;
                    memmap++;
                }
            }
        }
    }
}

void ibex_connect_devices(DeviceState **devices, const IbexDeviceDef *defs,
                          unsigned count)
{
    /* Connect GPIOs (in particular, IRQs) */
    for (unsigned idx = 0; idx < count; idx++) {
        DeviceState *dev = devices[idx];
        const IbexDeviceDef *def = &defs[idx];

        if (def->gpio) {
            const IbexGpioConnDef *conn = def->gpio;
            while (conn->out.num >= 0 && conn->in.num >= 0) {
                qemu_irq in_gpio =
                    qdev_get_gpio_in_named(devices[conn->in.index],
                                           conn->in.name, conn->in.num);

                qdev_connect_gpio_out_named(dev, conn->out.name, conn->out.num,
                                            in_gpio);
                conn++;
            }
        }
    }
}

void ibex_configure_devices(DeviceState **devices, BusState *bus,
                            const IbexDeviceDef *defs, unsigned count)
{
    ibex_link_devices(devices, defs, count);
    ibex_define_device_props(devices, defs, count);
    ibex_realize_devices(devices, bus, defs, count);
    ibex_connect_devices(devices, defs, count);
}

void ibex_unimp_configure(DeviceState *dev, const IbexDeviceDef *def,
                          DeviceState *parent)
{
    if (def->name) {
        qdev_prop_set_string(dev, "name", def->name);
    }
    assert(def->memmap != NULL);
    assert(def->memmap->size != 0);
    qdev_prop_set_uint64(dev, "size", def->memmap->size);
}

void ibex_load_kernel(AddressSpace *as)
{
    MachineState *ms = MACHINE(qdev_get_machine());

    /* load kernel if provided */
    if (ms->kernel_filename) {
        uint64_t kernel_entry;
        if (load_elf_ram_sym(ms->kernel_filename, NULL, NULL, NULL,
                             &kernel_entry, NULL, NULL, NULL, 0, EM_RISCV, 1, 0,
                             as, true, NULL) <= 0) {
            error_report("Cannot load ELF kernel %s", ms->kernel_filename);
            exit(EXIT_FAILURE);
        }

        CPUState *cpu;
        CPU_FOREACH(cpu) {
            if (!as || cpu->as == as) {
                CPURISCVState *env = &RISCV_CPU(cpu)->env;
                env->resetvec = (target_ulong)kernel_entry;
            }
        }
    }
}

uint64_t ibex_get_current_pc(void)
{
    CPUState *cs = current_cpu;

    return cs && cs->cc->get_pc ? cs->cc->get_pc(cs) : 0u;
}

/* x0 is replaced with PC */
static const char ibex_ireg_names[32u][4u] = {
    "pc", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
    "a1", "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
};

void ibex_log_vcpu_registers(uint64_t regbm)
{
    CPURISCVState *env = &RISCV_CPU(current_cpu)->env;
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "\n....\n");
    if (regbm & 0x1u) {
        qemu_log_mask(CPU_LOG_TB_IN_ASM, "%4s: 0x" TARGET_FMT_lx "\n",
                      ibex_ireg_names[0], env->pc);
    }
    for (unsigned gix = 1u; gix < 32u; gix++) {
        uint64_t mask = 1u << gix;
        if (regbm & mask) {
            qemu_log_mask(CPU_LOG_TB_IN_ASM, "%4s: 0x" TARGET_FMT_lx "\n",
                          ibex_ireg_names[gix], env->gpr[gix]);
        }
    }
}

/*
 * Note: this is not specific to Ibex, and might apply to any vCPU.
 */
static void hmp_info_ibex(Monitor *mon, const QDict *qdict)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        vaddr pc;
        const char *symbol;
        if (cpu->cc->get_pc) {
            pc = cpu->cc->get_pc(cpu);
            symbol = lookup_symbol(pc);
        } else {
            pc = -1;
            symbol = "?";
        }
        monitor_printf(mon, "* CPU #%d: 0x%" PRIx64 " in '%s'\n",
                       cpu->cpu_index, (uint64_t)pc, symbol);
    }
}

static void ibex_register_types(void)
{
    monitor_register_hmp("ibex", true, &hmp_info_ibex);
}

type_init(ibex_register_types)
