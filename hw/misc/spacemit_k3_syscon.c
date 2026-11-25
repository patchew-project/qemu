/*
 * Spacemit K3 Syscons
 *
 * Control clocks, resets, ddr and misc functionality
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "crypto/hash.h"
#include "hw/core/cpu.h"
#include "hw/misc/spacemit_k3_syscon.h"
#include "target/riscv/cpu.h"
#include "trace.h"

#define TO_REG(offset) (offset >> 2)

#define BOOT_MODE_VALUE_NOR 0x10000200
#define BOOT_MODE_VALUE_EMMC 0x10000000
#define BOOT_MODE_VALUE_NAND 0x10000400
#define BOOT_MODE_VALUE_UFS 0x10000600
#define BOOT_MODE_VALUE_USB 0x10001000
#define BOOT_MODE_VALUE_UART 0x10001800

#define SELECTED_BOOT_MODE_VALUE_EMMC 0xb00
#define SELECTED_BOOT_MODE_VALUE_NOR 0xb01
#define SELECTED_BOOT_MODE_VALUE_UFS 0xb03
#define SELECTED_BOOT_MODE_VALUE_SD 0xb10

/* CPU Config register offsets */
#define BOOT_PIN_SELECT 0x20
#define BOOT_DEV_FLAG_REG 0x110
#define C0_RVBADDR_LO_ADDR 0x1b0
#define C0_RVBADDR_HI_ADDR 0x1b4
#define C1_RVBADDR_LO_ADDR 0x2b0
#define C1_RVBADDR_HI_ADDR 0x2b4

/* Syscon APMU register offsets */
#define PMU_AP_CORE0_WAKEUP 0x12c
#define PMU_AP_CORE1_WAKEUP 0x130
#define PMU_AP_CORE2_WAKEUP 0x134
#define PMU_AP_CORE3_WAKEUP 0x138
#define APMU_RCPU_CLK_RES_CTRL 0x14c
#define PMU_AP_CORE4_WAKEUP 0x324
#define PMU_AP_CORE5_WAKEUP 0x328
#define PMU_AP_CORE6_WAKEUP 0x32c
#define PMU_AP_CORE7_WAKEUP 0x330

/* Syscon MPMU register offsets */
#define MPMU_POSR 0x10

#define SPACEMIT_K3_IMSIC_MSTRIDE 0x1000

#define RVBA_LO(cpu_idx) TO_REG((C0_RVBADDR_LO_ADDR + ((cpu_idx >> 2) * 0x100)))
#define RVBA_HI(cpu_idx) TO_REG((C0_RVBADDR_HI_ADDR + ((cpu_idx >> 2) * 0x100)))

static void spacemit_k3_syscon_apmu_release_hart(CPUState *cs,
                                               run_on_cpu_data data)
{
    cpu_reset(cs);
    cs->halted = 0;
    qemu_cpu_kick(cs);
}

static uint64_t spacemit_k3_syscon_mpmu_read(void *opaque, hwaddr offset,
                                             unsigned size)
{
    uint64_t ret = 0;

    switch (offset) {
    case MPMU_POSR:
        ret = 0xff0000;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (size %u, offset 0x%"
                      HWADDR_PRIx ")\n", TYPE_SPACEMIT_K3_SYSCON_MPMU_UNIT,
                      size, offset);
        break;
    }

    trace_spacemit_k3_syscon_mpmu_read(offset, size, ret);
    return ret;
}

static void spacemit_k3_syscon_mpmu_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    trace_spacemit_k3_syscon_mpmu_write(offset, size, value);
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (size %u, offset 0x%"
                  HWADDR_PRIx ", value 0x%" PRIx64 ")\n",
                  TYPE_SPACEMIT_K3_SYSCON_MPMU_UNIT, size, offset, value);
}

static uint64_t spacemit_k3_syscon_apmu_read(void *opaque, hwaddr offset,
                                             unsigned size)
{
    uint64_t ret = 0;

    switch (offset) {
    case APMU_RCPU_CLK_RES_CTRL:
        ret = BIT(23);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (size %u, offset 0x%"
                      HWADDR_PRIx ")\n", TYPE_SPACEMIT_K3_SYSCON_APMU_UNIT,
                      size, offset);
        break;
    }

    trace_spacemit_k3_syscon_apmu_read(offset, size, ret);
    return ret;
}

static void spacemit_k3_syscon_apmu_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    SpacemitK3SysconAPMUState *s = SPACEMIT_K3_SYSCON_APMU_UNIT(opaque);
    int cpu_idx = -1;

    trace_spacemit_k3_syscon_apmu_write(offset, size, value);

    switch (offset) {
    case PMU_AP_CORE1_WAKEUP:
    case PMU_AP_CORE2_WAKEUP:
    case PMU_AP_CORE3_WAKEUP:
        cpu_idx = (offset - PMU_AP_CORE0_WAKEUP) >> 2;
        break;
    case PMU_AP_CORE4_WAKEUP:
    case PMU_AP_CORE5_WAKEUP:
    case PMU_AP_CORE6_WAKEUP:
    case PMU_AP_CORE7_WAKEUP:
        cpu_idx = 4 + ((offset - PMU_AP_CORE4_WAKEUP) >> 2);
        break;
    default:
      qemu_log_mask(LOG_UNIMP,
                    "%s: unimplemented write (size %u, offset 0x%" HWADDR_PRIx
                    ", value 0x%" PRIx64 ")\n",
                    TYPE_SPACEMIT_K3_SYSCON_APMU_UNIT, size, offset, value);
      break;
    }

    /* Release non-boot HART from reset */
    if (cpu_idx != -1 && !s->released[cpu_idx]) {
        CPUState *cs = s->cpus[cpu_idx];
        s->released[cpu_idx] = true;

        trace_spacemit_k3_syscon_apmu_cpu_release(cpu_idx);
        async_safe_run_on_cpu(cs, spacemit_k3_syscon_apmu_release_hart,
                              RUN_ON_CPU_NULL);
    }
}

static void spacemit_k3_syscon_apmu_overlay_write(void *opaque, hwaddr offset,
                                                  uint64_t value, unsigned size)
{
    SpacemitK3SysconAPMUState *s = SPACEMIT_K3_SYSCON_APMU_UNIT(opaque);
    unsigned int hart = offset / SPACEMIT_K3_IMSIC_MSTRIDE;
    hwaddr page_offset = offset % SPACEMIT_K3_IMSIC_MSTRIDE;

    if (hart >= SPACEMIT_K3_MAX_NUM_HARTS ||
        s->mimsic_regions[hart] == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid overlay write (hart %u, offset 0x%"
                      HWADDR_PRIx ", value 0x%" PRIx64 ")\n",
                      TYPE_SPACEMIT_K3_SYSCON_APMU_UNIT, hart, offset, value);
        return;
    }

    memory_region_dispatch_write(s->mimsic_regions[hart], page_offset, value,
                                 MO_32, MEMTXATTRS_UNSPECIFIED);
    trace_spacemit_k3_syscon_apmu_overlay_write(hart, offset, value);

    if (!s->released[hart]) {
        CPUState *cs = s->cpus[hart];
        s->released[hart] = true;

        trace_spacemit_k3_syscon_apmu_cpu_release(hart);
        async_safe_run_on_cpu(cs, spacemit_k3_syscon_apmu_release_hart,
                              RUN_ON_CPU_NULL);
    }
}

static uint64_t spacemit_k3_ddrc_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    SpacemitK3DDRCState *s = opaque;
    uint64_t ret;

    /*
     * Register offsets and names are not known
     * Values here are set based on what
     * DDR fw polls for and what bits it expects
     * to be set.
     */
    switch (offset) {
    case 0x10014:
        ret = 1;
        break;
    case 0x10514:
        ret = 1;
        break;
    case 0x10c84:
        ret = 1;
        break;
    case 0x60001c:
        ret = 1;
        break;
    case 0xb40010:
        ret = s->b40010;
        break;
    case 0xb400c8:
        ret = 7;
        break;
    default:
        ret = 0;
        break;
    }

    return ret;
}

static void spacemit_k3_ddrc_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    SpacemitK3DDRCState *s = opaque;

    switch (offset) {
    case 0xb400c4:
        s->b40010 = value == 0 ? 1 : 0;
        break;
    }
}

static uint64_t spacemit_k3_cpu_config_read(void *opaque, hwaddr offset,
                                            unsigned size)
{
    SpacemitK3CPUConfigState *s = SPACEMIT_K3_CPU_CONFIG(opaque);
    uint64_t ret = s->regs[TO_REG(offset)];

    trace_spacemit_k3_cpu_config_read(offset, size, ret);
    return ret;
}

static void spacemit_k3_cpu_config_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    SpacemitK3CPUConfigState *s = SPACEMIT_K3_CPU_CONFIG(opaque);
    int cluster_idx = -1;

    trace_spacemit_k3_cpu_config_write(offset, size, value);

    s->regs[TO_REG(offset)] = value;

    switch (offset) {
    case C0_RVBADDR_LO_ADDR:
    case C0_RVBADDR_HI_ADDR:
        cluster_idx = 0;
        break;
    case C1_RVBADDR_LO_ADDR:
    case C1_RVBADDR_HI_ADDR:
        cluster_idx = 1;
        break;
    }

    if (cluster_idx != -1) {
        for (int cpu_idx = cluster_idx * 4; cpu_idx < cluster_idx * 4 + 4;
           cpu_idx++) {
            cpu_set_exception_base(cpu_idx,
                                   s->regs[RVBA_LO(cpu_idx)] |
                                   ((uint64_t)s->regs[RVBA_HI(cpu_idx)]) << 32);
        }
    }
}

static uint32_t spacemit_k3_boot_mode_value(int boot_mode)
{
    static const uint32_t boot_mode_values[] = {
      [SPACEMIT_K3_BOOT_MODE_EMMC] = BOOT_MODE_VALUE_EMMC,
      [SPACEMIT_K3_BOOT_MODE_NOR] = BOOT_MODE_VALUE_NOR,
      [SPACEMIT_K3_BOOT_MODE_NAND] = BOOT_MODE_VALUE_NAND,
      [SPACEMIT_K3_BOOT_MODE_UFS] = BOOT_MODE_VALUE_UFS,
      [SPACEMIT_K3_BOOT_MODE_USB] = BOOT_MODE_VALUE_USB,
      [SPACEMIT_K3_BOOT_MODE_UART] = BOOT_MODE_VALUE_UART,
    };

    if (boot_mode < 0 || boot_mode >= ARRAY_SIZE(boot_mode_values)) {
        return BOOT_MODE_VALUE_NOR;
    }

    return boot_mode_values[boot_mode];
}

void spacemit_k3_set_selected_boot_mode(SpacemitK3CPUConfigState *s,
                                        int selected_boot_mode) {
    static const uint32_t selected_boot_mode_values[] = {
      [SPACEMIT_K3_SELECTED_BOOT_MODE_EMMC] =
          SELECTED_BOOT_MODE_VALUE_EMMC,
      [SPACEMIT_K3_SELECTED_BOOT_MODE_NOR] =
          SELECTED_BOOT_MODE_VALUE_NOR,
      [SPACEMIT_K3_SELECTED_BOOT_MODE_UFS] =
          SELECTED_BOOT_MODE_VALUE_UFS,
      [SPACEMIT_K3_SELECTED_BOOT_MODE_SD] =
          SELECTED_BOOT_MODE_VALUE_SD,
    };

    if (selected_boot_mode < 0 ||
          selected_boot_mode >= ARRAY_SIZE(selected_boot_mode_values)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad selected boot mode %d\n",
                      __func__,
                      selected_boot_mode);
        return;
    }

    s->regs[TO_REG(BOOT_DEV_FLAG_REG)] =
      selected_boot_mode_values[selected_boot_mode];
}

static const MemoryRegionOps spacemit_k3_syscon_mpmu_ops = {
    .read = spacemit_k3_syscon_mpmu_read,
    .write = spacemit_k3_syscon_mpmu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps spacemit_k3_syscon_apmu_ops = {
    .read = spacemit_k3_syscon_apmu_read,
    .write = spacemit_k3_syscon_apmu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps spacemit_k3_syscon_apmu_overlay_ops = {
    .write = spacemit_k3_syscon_apmu_overlay_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps spacemit_k3_ddrc_ops = {
    .read = spacemit_k3_ddrc_read,
    .write = spacemit_k3_ddrc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps spacemit_k3_cpu_config_ops = {
    .read = spacemit_k3_cpu_config_read,
    .write = spacemit_k3_cpu_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void spacemit_k3_syscon_mpmu_init(Object *obj)
{
    SpacemitK3SysconMPMUState *s = SPACEMIT_K3_SYSCON_MPMU_UNIT(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &spacemit_k3_syscon_mpmu_ops, s,
                          TYPE_SPACEMIT_K3_SYSCON_MPMU_UNIT,
                          SPACEMIT_K3_SYSCON_MPMU_SIZE);
    sysbus_init_mmio(dev, &s->mmio);
}

static void spacemit_k3_syscon_apmu_init(Object *obj)
{
    SpacemitK3SysconAPMUState *s = SPACEMIT_K3_SYSCON_APMU_UNIT(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &spacemit_k3_syscon_apmu_ops, s,
                          TYPE_SPACEMIT_K3_SYSCON_APMU_UNIT,
                          SPACEMIT_K3_SYSCON_APMU_SIZE);
    sysbus_init_mmio(dev, &s->mmio);

    memory_region_init_io(&s->mimsic_overlay, obj,
                          &spacemit_k3_syscon_apmu_overlay_ops,
                          s, "k3.apmu.imsic-overlay",
                          0x10000);
}

static void spacemit_k3_ddrc_init(Object *obj)
{
    SpacemitK3DDRCState *s = SPACEMIT_K3_DDRC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &spacemit_k3_ddrc_ops, s,
                          TYPE_SPACEMIT_K3_DDRC, SPACEMIT_K3_DDRC_SIZE);
    sysbus_init_mmio(dev, &s->mmio);

    s->b40010 = 0;
}

static void spacemit_k3_cpu_config_init(Object *obj)
{
    SpacemitK3CPUConfigState *s = SPACEMIT_K3_CPU_CONFIG(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &spacemit_k3_cpu_config_ops, s,
                          TYPE_SPACEMIT_K3_CPU_CONFIG,
                          SPACEMIT_K3_CPU_CONFIG_SIZE);
    sysbus_init_mmio(dev, &s->mmio);
}

static void spacemit_k3_ddrc_reset_hold(Object *obj, ResetType type)
{
    SpacemitK3DDRCState *s = SPACEMIT_K3_DDRC(obj);

    s->b40010 = 0;
}

static void spacemit_k3_ddrc_class_init(ObjectClass *oc,
                                               const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.hold = spacemit_k3_ddrc_reset_hold;
}

static void spacemit_k3_syscon_apmu_reset_hold(Object *obj, ResetType type)
{
    SpacemitK3SysconAPMUState *s = SPACEMIT_K3_SYSCON_APMU_UNIT(obj);

    memset(s->released, 0, sizeof(s->released));

    /* Mark boot hart as released from reset */
    s->released[0] = true;
}

static const Property spacemit_k3_syscon_apmu_properties[] = {
    DEFINE_PROP_LINK_ARRAY("cpus", SpacemitK3SysconAPMUState, num_cpus, cpus,
                           TYPE_CPU, CPUState *),
    DEFINE_PROP_LINK_ARRAY("mimsic-regions", SpacemitK3SysconAPMUState,
                              num_imsics, mimsic_regions,
                              TYPE_MEMORY_REGION, MemoryRegion *),
};

static void spacemit_k3_syscon_apmu_class_init(ObjectClass *oc,
                                               const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_props(dc, spacemit_k3_syscon_apmu_properties);
    rc->phases.hold = spacemit_k3_syscon_apmu_reset_hold;
}

static void spacemit_k3_cpu_config_reset_hold(Object *obj, ResetType type)
{
    SpacemitK3CPUConfigState *s = SPACEMIT_K3_CPU_CONFIG(obj);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[TO_REG(BOOT_PIN_SELECT)] =
        spacemit_k3_boot_mode_value(s->boot_mode);
}

static const Property spacemit_k3_cpu_config_properties[] = {
    DEFINE_PROP_INT32("boot-mode", SpacemitK3CPUConfigState, boot_mode,
                      SPACEMIT_K3_BOOT_MODE_NOR),
    DEFINE_PROP_LINK_ARRAY("cpus", SpacemitK3CPUConfigState, num_cpus, cpus,
                           TYPE_CPU, CPUState *),
};

static void spacemit_k3_cpu_config_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, spacemit_k3_cpu_config_properties);
    rc->phases.hold = spacemit_k3_cpu_config_reset_hold;
};

static const TypeInfo spacemit_k3_syscon_types[] = {
    {
        .name = TYPE_SPACEMIT_K3_SYSCON_MPMU_UNIT,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(SpacemitK3SysconMPMUState),
        .instance_init = spacemit_k3_syscon_mpmu_init,
    },
    {
        .name = TYPE_SPACEMIT_K3_SYSCON_APMU_UNIT,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(SpacemitK3SysconAPMUState),
        .instance_init = spacemit_k3_syscon_apmu_init,
        .class_init = spacemit_k3_syscon_apmu_class_init,
    },
    {
        .name = TYPE_SPACEMIT_K3_DDRC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(SpacemitK3DDRCState),
        .instance_init = spacemit_k3_ddrc_init,
        .class_init = spacemit_k3_ddrc_class_init,
    },
    {
        .name = TYPE_SPACEMIT_K3_CPU_CONFIG,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(SpacemitK3CPUConfigState),
        .instance_init = spacemit_k3_cpu_config_init,
        .class_init = spacemit_k3_cpu_config_class_init,
    },
};
DEFINE_TYPES(spacemit_k3_syscon_types);
