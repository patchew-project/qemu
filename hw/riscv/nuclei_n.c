/*
 * Nuclei N series  SOC machine interface
 *
 * Copyright (c) 2020 Gao ZhiYuan <alapha23@gmail.com>
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/misc/unimp.h"
#include "hw/char/riscv_htif.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/char/nuclei_uart.h"
#include "hw/riscv/nuclei_n.h"
#include "hw/riscv/boot.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/qtest.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#include <libfdt.h>

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} nuclei_memmap[] = {
    [HBIRD_DEBUG] = {0x0, 0x1000},
    [HBIRD_ROM] = {0x1000, 0x1000},
    [HBIRD_TIMER] = {0x2000000, 0x1000},
    [HBIRD_ECLIC] = {0xc000000, 0x10000},
    [HBIRD_GPIO] = {0x10012000, 0x1000},
    [HBIRD_UART0] = {0x10013000, 0x1000},
    [HBIRD_QSPI0] = {0x10014000, 0x1000},
    [HBIRD_PWM0] = {0x10015000, 0x1000},
    [HBIRD_UART1] = {0x10023000, 0x1000},
    [HBIRD_QSPI1] = {0x10024000, 0x1000},
    [HBIRD_PWM1] = {0x10025000, 0x1000},
    [HBIRD_QSPI2] = {0x10034000, 0x1000},
    [HBIRD_PWM2] = {0x10035000, 0x1000},
    [HBIRD_XIP] = {0x20000000, 0x10000000},
    [HBIRD_DRAM] = {0xa0000000, 0x0},
    [HBIRD_ILM] = {0x80000000, 0x20000},
    [HBIRD_DLM] = {0x90000000, 0x20000},
};

static void nuclei_machine_get_uint32_prop(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    visit_type_uint32(v, name, (uint32_t *)opaque, errp);
}

static void nuclei_machine_set_uint32_prop(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    visit_type_uint32(v, name, (uint32_t *)opaque, errp);
}

static void nuclei_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = nuclei_memmap;
    NucleiHBState *s = HBIRD_FPGA_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    target_ulong start_addr = memmap[HBIRD_ILM].base;
    int i;

    /* TODO: Add qtest support */
    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc",
                    &s->soc, TYPE_NUCLEI_HBIRD_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    memory_region_init_ram(&s->soc.ilm, NULL, "riscv.nuclei.ram.ilm",
                           memmap[HBIRD_ILM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[HBIRD_ILM].base, &s->soc.ilm);

    memory_region_init_ram(&s->soc.dlm, NULL, "riscv.nuclei.ram.dlm",
                           memmap[HBIRD_DLM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[HBIRD_DLM].base, &s->soc.dlm);

    /* register DRAM */
    memory_region_init_ram(main_mem, NULL, "riscv.nuclei.dram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[HBIRD_DRAM].base,
                                main_mem);

    /* Flash memory */
    memory_region_init_ram(flash, NULL, "riscv.nuclei.xip",
                           memmap[HBIRD_XIP].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[HBIRD_XIP].base,
                                flash);

    switch (s->msel) {
    case MSEL_ILM:
        start_addr = memmap[HBIRD_ILM].base;
        break;
    case MSEL_FLASH:
        start_addr = memmap[HBIRD_XIP].base;
        break;
    case MSEL_FLASHXIP:
        start_addr = memmap[HBIRD_XIP].base;
        break;
    case MSEL_DDR:
        start_addr = memmap[HBIRD_DRAM].base;
        break;
    default:
        start_addr = memmap[HBIRD_ILM].base;
        break;
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297, /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593, /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573, /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283, /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283, /*     ld     t0, 24(t0) */
#endif
        0x00028067, /*     jr     t0 */
        0x00000000,
        start_addr, /* start: .dword DRAM_BASE */
        0x00000000,
    };

    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[HBIRD_ROM].base, &address_space_memory);

    /* boot rom */
    if (machine->kernel_filename) {
        riscv_load_kernel(machine->kernel_filename, start_addr, NULL);
    }
}

static void nuclei_soc_init(Object *obj)
{
    NucleiHBSoCState *s = RISCV_NUCLEI_HBIRD_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);

    object_initialize_child(obj, "riscv.nuclei.gpio",
                            &s->gpio, TYPE_SIFIVE_GPIO);
}

static void nuclei_soc_realize(DeviceState *dev, Error **errp)
{
    const struct MemmapEntry *memmap = nuclei_memmap;
    MachineState *ms = MACHINE(qdev_get_machine());
    NucleiHBSoCState *s = RISCV_NUCLEI_HBIRD_SOC(dev);
    MemoryRegion *sys_mem = get_system_memory();
    Error *err = NULL;

    object_property_set_str(OBJECT(&s->cpus), "cpu-type", ms->cpu_type,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);

    /* Mask ROM */
    memory_region_init_rom(&s->internal_rom, OBJECT(dev), "riscv.nuclei.irom",
                           memmap[HBIRD_ROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
                                memmap[HBIRD_ROM].base, &s->internal_rom);

    s->eclic = nuclei_eclic_create(memmap[HBIRD_ECLIC].base,
                                   memmap[HBIRD_ECLIC].size, HBIRD_SOC_INT_MAX);

    s->timer = nuclei_systimer_create(memmap[HBIRD_TIMER].base,
                                      memmap[HBIRD_TIMER].size,
                                      s->eclic,
                                      NUCLEI_HBIRD_TIMEBASE_FREQ);

    /* GPIO */
    sysbus_realize(SYS_BUS_DEVICE(&s->gpio), &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, memmap[HBIRD_GPIO].base);

    nuclei_uart_create(sys_mem,
                       memmap[HBIRD_UART0].base,
                       memmap[HBIRD_UART0].size,
                       serial_hd(0),
                       nuclei_eclic_get_irq(DEVICE(s->eclic),
                                            HBIRD_SOC_INT22_IRQn));
}

static void nuclei_machine_instance_init(Object *obj)
{
    NucleiHBState *s = HBIRD_FPGA_MACHINE(obj);

    s->msel = 0;
    object_property_add(obj, "msel", "uint32",
                        nuclei_machine_get_uint32_prop,
                        nuclei_machine_set_uint32_prop, NULL, &s->msel);
    object_property_set_description(obj, "msel",
                                    "Mode Select Startup");
}

static void nuclei_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nuclei HummingBird Evaluation Kit";
    mc->init = nuclei_board_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = NUCLEI_N_CPU;
}

static const TypeInfo nuclei_machine_typeinfo = {
    .name = MACHINE_TYPE_NAME("hbird_fpga"),
    .parent = TYPE_MACHINE,
    .class_init = nuclei_machine_class_init,
    .instance_init = nuclei_machine_instance_init,
    .instance_size = sizeof(NucleiHBState),
};

static void nuclei_machine_init_register_types(void)
{
    type_register_static(&nuclei_machine_typeinfo);
}

type_init(nuclei_machine_init_register_types)

    static void nuclei_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = nuclei_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo nuclei_soc_type_info = {
    .name = TYPE_NUCLEI_HBIRD_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NucleiHBSoCState),
    .instance_init = nuclei_soc_init,
    .class_init = nuclei_soc_class_init,
};

static void nuclei_soc_register_types(void)
{
    type_register_static(&nuclei_soc_type_info);
}

type_init(nuclei_soc_register_types)
