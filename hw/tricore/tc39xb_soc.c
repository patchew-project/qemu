/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Infineon TC39x SoC System emulation.
 *
 * Copyright (c) 2020 Andreas Konopik <andreas.konopik@efs-auto.de>
 * Copyright (c) 2020 David Brenken <david.brenken@efs-auto.de>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/clock.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/sysbus.h"
#include "hw/core/loader.h"
#include "qemu/units.h"
#include "hw/misc/unimp.h"
#include "system/system.h"

#include "hw/tricore/tc39xb_soc.h"
#include "hw/tricore/tc27x_soc.h"

const MemmapEntry tc39xb_soc_memmap[] = {
    [TC39XB_DSPR5]     = { 0x10000000,            240 * KiB },
    [TC39XB_DCACHE5]   = { 0x10018000,             16 * KiB },
    [TC39XB_DTAG5]     = { 0x100C0000,              6 * KiB },
    [TC39XB_PSPR5]     = { 0x10100000,             64 * KiB },
    [TC39XB_PCACHE5]   = { 0x10108000,             32 * KiB },
    [TC39XB_PTAG5]     = { 0x101C0000,             12 * KiB },
    [TC39XB_DSPR4]     = { 0x30000000,            240 * KiB },
    [TC39XB_DCACHE4]   = { 0x30018000,             16 * KiB },
    [TC39XB_DTAG4]     = { 0x300C0000,              6 * KiB },
    [TC39XB_PSPR4]     = { 0x30100000,             64 * KiB },
    [TC39XB_PCACHE4]   = { 0x30108000,             32 * KiB },
    [TC39XB_PTAG4]     = { 0x301C0000,             12 * KiB },
    [TC39XB_DSPR3]     = { 0x40000000,            240 * KiB },
    [TC39XB_DCACHE3]   = { 0x40018000,             16 * KiB },
    [TC39XB_DTAG3]     = { 0x400C0000,              6 * KiB },
    [TC39XB_PSPR3]     = { 0x40100000,             64 * KiB },
    [TC39XB_PCACHE3]   = { 0x40108000,             32 * KiB },
    [TC39XB_PTAG3]     = { 0x401C0000,             12 * KiB },
    [TC39XB_DSPR2]     = { 0x50000000,            240 * KiB },
    [TC39XB_DCACHE2]   = { 0x5001E000,             16 * KiB },
    [TC39XB_DTAG2]     = { 0x500C0000,              6 * KiB },
    [TC39XB_PSPR2]     = { 0x50100000,             64 * KiB },
    [TC39XB_PCACHE2]   = { 0x50108000,             32 * KiB },
    [TC39XB_PTAG2]     = { 0x501C0000,             12 * KiB },
    [TC39XB_DSPR1]     = { 0x60000000,            240 * KiB },
    [TC39XB_DCACHE1]   = { 0x6001E000,             16 * KiB },
    [TC39XB_DTAG1]     = { 0x600C0000,              6 * KiB },
    [TC39XB_PSPR1]     = { 0x60100000,             64 * KiB },
    [TC39XB_PCACHE1]   = { 0x60108000,             32 * KiB },
    [TC39XB_PTAG1]     = { 0x601C0000,             12 * KiB },
    [TC39XB_DSPR0]     = { 0x70000000,            240 * KiB },
    [TC39XB_DCACHE0]   = { 0x7001E000,             16 * KiB },
    [TC39XB_DTAG0]     = { 0x700C0000,              6 * KiB },
    [TC39XB_PSPR0]     = { 0x70100000,             64 * KiB },
    [TC39XB_PCACHE0]   = { 0x70108000,             32 * KiB },
    [TC39XB_PTAG0]     = { 0x701C0000,             12 * KiB },
    [TC39XB_PFLASH0_C] = { 0x80000000,              3 * MiB },
    [TC39XB_PFLASH1_C] = { 0x80300000,              3 * MiB },
    [TC39XB_PFLASH2_C] = { 0x80600000,              3 * MiB },
    [TC39XB_PFLASH3_C] = { 0x80900000,              3 * MiB },
    [TC39XB_PFLASH4_C] = { 0x80C00000,              3 * MiB },
    [TC39XB_PFLASH5_C] = { 0x80F00000,              1 * MiB },
    [TC39XB_OLDA_C]    = { 0x8FE00000,            512 * KiB },
    [TC39XB_BROM_C]    = { 0x8FFF0000,             64 * KiB },
    [TC39XB_DLMU0_C]   = { 0x90000000,             64 * KiB },
    [TC39XB_DLMU1_C]   = { 0x90010000,             64 * KiB },
    [TC39XB_DLMU2_C]   = { 0x90020000,             64 * KiB },
    [TC39XB_DLMU3_C]   = { 0x90030000,             64 * KiB },
    [TC39XB_LMU0_C]    = { 0x90040000,            256 * KiB },
    [TC39XB_LMU1_C]    = { 0x90080000,            256 * KiB },
    [TC39XB_LMU2_C]    = { 0x900C0000,            256 * KiB },
    [TC39XB_DLMU4_C]   = { 0x90100000,             64 * KiB },
    [TC39XB_DLMU5_C]   = { 0x90110000,             64 * KiB },
    [TC39XB_EMEM]      = { 0x99000000,              4 * MiB },
    [TC39XB_PFLASH0_U] = { 0xA0000000,                  0x0 },
    [TC39XB_PFLASH1_U] = { 0xA0300000,                  0x0 },
    [TC39XB_PFLASH2_U] = { 0xA0600000,                  0x0 },
    [TC39XB_PFLASH3_U] = { 0xA0900000,                  0x0 },
    [TC39XB_PFLASH4_U] = { 0xA0C00000,                  0x0 },
    [TC39XB_PFLASH5_U] = { 0xA0F00000,                  0x0 },
    [TC39XB_DFLASH0]   = { 0xAF000000,              1 * MiB },
    [TC39XB_DFLASH1]   = { 0xAFC00000,            128 * KiB },
    [TC39XB_OLDA_U]    = { 0xAFE00000,                  0x0 },
    [TC39XB_BROM_U]    = { 0xAFFF0000,                  0x0 },
    [TC39XB_DLMU0_U]   = { 0xB0000000,                  0x0 },
    [TC39XB_DLMU1_U]   = { 0xB0010000,                  0x0 },
    [TC39XB_DLMU2_U]   = { 0xB0020000,                  0x0 },
    [TC39XB_DLMU3_U]   = { 0xB0030000,                  0x0 },
    [TC39XB_LMU0_U]    = { 0xB0040000,                  0x0 },
    [TC39XB_LMU1_U]    = { 0xB0080000,                  0x0 },
    [TC39XB_LMU2_U]    = { 0xB00C0000,                  0x0 },
    [TC39XB_DLMU4_U]   = { 0xB0100000,                  0x0 },
    [TC39XB_DLMU5_U]   = { 0xB0110000,                  0x0 },
    [TC39XB_PSPRX]     = { 0xC0000000,                  0x0 },
    [TC39XB_DSPRX]     = { 0xD0000000,                  0x0 },
    [TC39XB_IR_INT]    = { 0xF0037000,                  0x0 },
    [TC39XB_IR_SRC]    = { 0xF0038000,                  0x0 },
    [TC39XB_STM0]      = { 0xF0001000,                  0x0 },
    [TC39XB_ASCLIN0]   = { 0xF0000600,                  0x0 },
    [TC39XB_SCU]       = { 0xF0036000,                  0x0 },
};

static void make_rom(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_rom(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void make_ram(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void make_alias(MemoryRegion *mr, const char *name,
                       MemoryRegion *orig, hwaddr base)
{
    memory_region_init_alias(mr, NULL, name, orig, 0,
                             memory_region_size(orig));
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void tc39xb_soc_init_memory_mapping(DeviceState *dev_soc)
{
    TC39XBSoCState *s = TC39XB_SOC(dev_soc);
    TC39XBSoCClass *sc = TC39XB_SOC_GET_CLASS(s);
    const MemmapEntry *map = sc->memmap;
    TC39XBSoCCPUMemState *c0 = &s->cpu0mem;
    TC39XBSoCCPUMemState *c1 = &s->cpu1mem;
    TC39XBSoCCPUMemState *c2 = &s->cpu2mem;
    TC39XBSoCCPUMemState *c3 = &s->cpu3mem;
    TC39XBSoCCPUMemState *c4 = &s->cpu4mem;
    TC39XBSoCCPUMemState *c5 = &s->cpu5mem;
    TC39XBSoCFlashMemState *f = &s->flashmem;

    /* CPU scratch-pad memories */
    make_ram(&c0->dspr, "CPU0.DSPR",
             map[TC39XB_DSPR0].base, map[TC39XB_DSPR0].size);
    make_ram(&c0->pspr, "CPU0.PSPR",
             map[TC39XB_PSPR0].base, map[TC39XB_PSPR0].size);
    make_ram(&c1->dspr, "CPU1.DSPR",
             map[TC39XB_DSPR1].base, map[TC39XB_DSPR1].size);
    make_ram(&c1->pspr, "CPU1.PSPR",
             map[TC39XB_PSPR1].base, map[TC39XB_PSPR1].size);
    make_ram(&c2->dspr, "CPU2.DSPR",
             map[TC39XB_DSPR2].base, map[TC39XB_DSPR2].size);
    make_ram(&c2->pspr, "CPU2.PSPR",
             map[TC39XB_PSPR2].base, map[TC39XB_PSPR2].size);
    make_ram(&c3->dspr, "CPU3.DSPR",
             map[TC39XB_DSPR3].base, map[TC39XB_DSPR3].size);
    make_ram(&c3->pspr, "CPU3.PSPR",
             map[TC39XB_PSPR3].base, map[TC39XB_PSPR3].size);
    make_ram(&c4->dspr, "CPU4.DSPR",
             map[TC39XB_DSPR4].base, map[TC39XB_DSPR4].size);
    make_ram(&c4->pspr, "CPU4.PSPR",
             map[TC39XB_PSPR4].base, map[TC39XB_PSPR4].size);
    make_ram(&c5->dspr, "CPU5.DSPR",
             map[TC39XB_DSPR5].base, map[TC39XB_DSPR5].size);
    make_ram(&c5->pspr, "CPU5.PSPR",
             map[TC39XB_PSPR5].base, map[TC39XB_PSPR5].size);

    /*
     * TriCore QEMU executes CPU0 only, thus it is sufficient to map
     * LOCAL.PSPR/LOCAL.DSPR exclusively onto PSPR0/DSPR0.
     */
    make_alias(&s->psprX, "LOCAL.PSPR",
               &c0->pspr, map[TC39XB_PSPRX].base);
    make_alias(&s->dsprX, "LOCAL.DSPR",
               &c0->dspr, map[TC39XB_DSPRX].base);

    /* Program flash (6 banks) */
    make_ram(&c0->pflash_c, "PF0",
             map[TC39XB_PFLASH0_C].base, map[TC39XB_PFLASH0_C].size);
    make_ram(&c1->pflash_c, "PF1",
             map[TC39XB_PFLASH1_C].base, map[TC39XB_PFLASH1_C].size);
    make_ram(&c2->pflash_c, "PF2",
             map[TC39XB_PFLASH2_C].base, map[TC39XB_PFLASH2_C].size);
    make_ram(&c3->pflash_c, "PF3",
             map[TC39XB_PFLASH3_C].base, map[TC39XB_PFLASH3_C].size);
    make_ram(&c4->pflash_c, "PF4",
             map[TC39XB_PFLASH4_C].base, map[TC39XB_PFLASH4_C].size);
    make_ram(&c5->pflash_c, "PF5",
             map[TC39XB_PFLASH5_C].base, map[TC39XB_PFLASH5_C].size);

    /* DLMU (per-CPU local memory) */
    make_ram(&c0->dlmu_c, "DLMU0",
             map[TC39XB_DLMU0_C].base, map[TC39XB_DLMU0_C].size);
    make_ram(&c1->dlmu_c, "DLMU1",
             map[TC39XB_DLMU1_C].base, map[TC39XB_DLMU1_C].size);
    make_ram(&c2->dlmu_c, "DLMU2",
             map[TC39XB_DLMU2_C].base, map[TC39XB_DLMU2_C].size);
    make_ram(&c3->dlmu_c, "DLMU3",
             map[TC39XB_DLMU3_C].base, map[TC39XB_DLMU3_C].size);
    make_ram(&c4->dlmu_c, "DLMU4",
             map[TC39XB_DLMU4_C].base, map[TC39XB_DLMU4_C].size);
    make_ram(&c5->dlmu_c, "DLMU5",
             map[TC39XB_DLMU5_C].base, map[TC39XB_DLMU5_C].size);

    /* Shared memories */
    make_ram(&f->dflash0, "DF0",
             map[TC39XB_DFLASH0].base, map[TC39XB_DFLASH0].size);
    make_ram(&f->dflash1, "DF1",
             map[TC39XB_DFLASH1].base, map[TC39XB_DFLASH1].size);
    make_ram(&f->olda_c, "OLDA",
             map[TC39XB_OLDA_C].base, map[TC39XB_OLDA_C].size);
    make_rom(&f->brom_c, "BROM",
             map[TC39XB_BROM_C].base, map[TC39XB_BROM_C].size);
    make_ram(&f->lmu0_c, "LMU0",
             map[TC39XB_LMU0_C].base, map[TC39XB_LMU0_C].size);
    make_ram(&f->lmu1_c, "LMU1",
             map[TC39XB_LMU1_C].base, map[TC39XB_LMU1_C].size);
    make_ram(&f->lmu2_c, "LMU2",
             map[TC39XB_LMU2_C].base, map[TC39XB_LMU2_C].size);
    make_ram(&f->emem, "EMEM",
             map[TC39XB_EMEM].base, map[TC39XB_EMEM].size);

    /* Uncached aliases for program flash */
    make_alias(&c0->pflash_u, "PF0.U",
               &c0->pflash_c, map[TC39XB_PFLASH0_U].base);
    make_alias(&c1->pflash_u, "PF1.U",
               &c1->pflash_c, map[TC39XB_PFLASH1_U].base);
    make_alias(&c2->pflash_u, "PF2.U",
               &c2->pflash_c, map[TC39XB_PFLASH2_U].base);
    make_alias(&c3->pflash_u, "PF3.U",
               &c3->pflash_c, map[TC39XB_PFLASH3_U].base);
    make_alias(&c4->pflash_u, "PF4.U",
               &c4->pflash_c, map[TC39XB_PFLASH4_U].base);
    make_alias(&c5->pflash_u, "PF5.U",
               &c5->pflash_c, map[TC39XB_PFLASH5_U].base);

    /* Uncached aliases for DLMU */
    make_alias(&c0->dlmu_u, "DLMU0.U",
               &c0->dlmu_c, map[TC39XB_DLMU0_U].base);
    make_alias(&c1->dlmu_u, "DLMU1.U",
               &c1->dlmu_c, map[TC39XB_DLMU1_U].base);
    make_alias(&c2->dlmu_u, "DLMU2.U",
               &c2->dlmu_c, map[TC39XB_DLMU2_U].base);
    make_alias(&c3->dlmu_u, "DLMU3.U",
               &c3->dlmu_c, map[TC39XB_DLMU3_U].base);
    make_alias(&c4->dlmu_u, "DLMU4.U",
               &c4->dlmu_c, map[TC39XB_DLMU4_U].base);
    make_alias(&c5->dlmu_u, "DLMU5.U",
               &c5->dlmu_c, map[TC39XB_DLMU5_U].base);

    /* Uncached aliases for shared memories */
    make_alias(&f->olda_u, "OLDA.U",
               &f->olda_c, map[TC39XB_OLDA_U].base);
    make_alias(&f->brom_u, "BROM.U",
               &f->brom_c, map[TC39XB_BROM_U].base);
    make_alias(&f->lmu0_u, "LMU0.U",
               &f->lmu0_c, map[TC39XB_LMU0_U].base);
    make_alias(&f->lmu1_u, "LMU1.U",
               &f->lmu1_c, map[TC39XB_LMU1_U].base);
    make_alias(&f->lmu2_u, "LMU2.U",
               &f->lmu2_c, map[TC39XB_LMU2_U].base);
}

/* TC39x interrupt source indices */
#define TC39X_SRC_STM0_SR0      0xC0
#define TC39X_SRC_ASCLIN0_TX    0x14
#define TC39X_SRC_ASCLIN0_RX    0x15
#define TC39X_SRC_ASCLIN0_ERR   0x16

static void tc39xb_soc_realize(DeviceState *dev_soc, Error **errp)
{
    TC39XBSoCState *s = TC39XB_SOC(dev_soc);
    TC39XBSoCClass *sc = TC39XB_SOC_GET_CLASS(s);
    Clock *fstm;
    Error *err = NULL;

    qdev_realize(DEVICE(&s->cpu), NULL, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    tc39xb_soc_init_memory_mapping(dev_soc);

    /* STM clock: 50 MHz */
    fstm = clock_new(OBJECT(dev_soc), "fstm");
    clock_set_hz(fstm, 50000000);
    qdev_connect_clock_in(DEVICE(&s->stm), "fstm", fstm);

    qdev_prop_set_chr(DEVICE(&s->asclin), "chardev", serial_hd(0));

    sysbus_realize(SYS_BUS_DEVICE(&s->ir), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->stm), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->asclin), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->scu), &error_fatal);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ir), 0,
                    sc->memmap[TC39XB_IR_INT].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ir), 1,
                    sc->memmap[TC39XB_IR_SRC].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->stm), 0,
                    sc->memmap[TC39XB_STM0].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->asclin), 0,
                    sc->memmap[TC39XB_ASCLIN0].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->scu), 0,
                    sc->memmap[TC39XB_SCU].base);

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->asclin), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ir), "irq",
                               TC39X_SRC_ASCLIN0_RX));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->asclin), 1,
        qdev_get_gpio_in_named(DEVICE(&s->ir), "irq",
                               TC39X_SRC_ASCLIN0_TX));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->asclin), 2,
        qdev_get_gpio_in_named(DEVICE(&s->ir), "irq",
                               TC39X_SRC_ASCLIN0_ERR));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->stm), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ir), "irq",
                               TC39X_SRC_STM0_SR0));
}

static void tc39xb_soc_init(Object *obj)
{
    TC39XBSoCState *s = TC39XB_SOC(obj);
    TC39XBSoCClass *sc = TC39XB_SOC_GET_CLASS(s);

    object_initialize_child(obj, "tc39x", &s->cpu, sc->cpu_type);
    object_initialize_child(obj, "ir", &s->ir, TYPE_TRICORE_IR);
    object_initialize_child(obj, "stm", &s->stm, TYPE_TRICORE_STM);
    object_initialize_child(obj, "asclin", &s->asclin, TYPE_TRICORE_ASCLIN);
    object_initialize_child(obj, "scu", &s->scu, TYPE_TRICORE_SCU);
}

static void tc39xb_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tc39xb_soc_realize;
}

static void tc397b_soc_class_init(ObjectClass *oc, const void *data)
{
    TC39XBSoCClass *sc = TC39XB_SOC_CLASS(oc);

    sc->name     = "tc397b-soc";
    sc->cpu_type = TRICORE_CPU_TYPE_NAME("tc39x");
    sc->memmap   = tc39xb_soc_memmap;
    sc->num_cpus = 1;
}

static const TypeInfo tc39xb_soc_types[] = {
    {
        .name          = "tc397b-soc",
        .parent        = TYPE_TC39XB_SOC,
        .class_init    = tc397b_soc_class_init,
    }, {
        .name          = TYPE_TC39XB_SOC,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(TC39XBSoCState),
        .instance_init = tc39xb_soc_init,
        .class_size    = sizeof(TC39XBSoCClass),
        .class_init    = tc39xb_soc_class_init,
        .abstract      = true,
    },
};

DEFINE_TYPES(tc39xb_soc_types)
