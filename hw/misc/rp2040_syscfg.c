/*
 * RP2040 syscfg emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_syscfg.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define SYSCFG_PROC0_NMI_MASK          0x00
#define SYSCFG_PROC1_NMI_MASK          0x04
#define SYSCFG_PROC_CONFIG             0x08
#define SYSCFG_PROC_IN_SYNC_BYPASS     0x0c
#define SYSCFG_PROC_IN_SYNC_BYPASS_HI  0x10
#define SYSCFG_DBGFORCE                0x14
#define SYSCFG_MEMPOWERDOWN            0x18

#define PROC_CONFIG_RW_MASK            0xff000000
#define PROC_CONFIG_RESET              0x10000000
#define PROC_IN_SYNC_BYPASS_MASK       0x3fffffff
#define PROC_IN_SYNC_BYPASS_HI_MASK    0x0000003f
#define DBGFORCE_RW_MASK               0x000000ee
#define DBGFORCE_RESET                 0x00000066
#define MEMPOWERDOWN_MASK              0x000000ff

#define ATOMIC_ALIAS_MASK              0x3000
#define ATOMIC_XOR                     0x1000
#define ATOMIC_SET                     0x2000
#define ATOMIC_CLR                     0x3000

static uint32_t rp2040_syscfg_apply_alias(uint32_t old, uint32_t value,
                                          hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static void rp2040_syscfg_update(RP2040SysCfgState *s)
{
    if (s->update) {
        s->update(s->update_opaque);
    }
}

void rp2040_syscfg_set_update_callback(RP2040SysCfgState *s,
                                       RP2040SysCfgUpdateFn update,
                                       void *opaque)
{
    s->update = update;
    s->update_opaque = opaque;
}

uint32_t rp2040_syscfg_get_proc0_nmi_mask(RP2040SysCfgState *s)
{
    return s->proc0_nmi_mask;
}

uint32_t rp2040_syscfg_get_mempowerdown(RP2040SysCfgState *s)
{
    return s->mempowerdown;
}

static uint64_t rp2040_syscfg_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040SysCfgState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    switch (offset) {
    case SYSCFG_PROC0_NMI_MASK:
        value = s->proc0_nmi_mask;
        break;
    case SYSCFG_PROC1_NMI_MASK:
        value = s->proc1_nmi_mask;
        break;
    case SYSCFG_PROC_CONFIG:
        value = s->proc_config;
        break;
    case SYSCFG_PROC_IN_SYNC_BYPASS:
        value = s->proc_in_sync_bypass;
        break;
    case SYSCFG_PROC_IN_SYNC_BYPASS_HI:
        value = s->proc_in_sync_bypass_hi;
        break;
    case SYSCFG_DBGFORCE:
        value = s->dbgforce;
        break;
    case SYSCFG_MEMPOWERDOWN:
        value = s->mempowerdown;
        break;
    default:
        value = 0;
        rp2040_log_unimplemented_read("syscfg", size,
                                      RP2040_SYSCFG_BASE + addr, offset,
                                      value);
        break;
    }

    return value;
}

static void rp2040_syscfg_write(void *opaque, hwaddr addr,
                                uint64_t value64, unsigned size)
{
    RP2040SysCfgState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t value = value64;

    switch (offset) {
    case SYSCFG_PROC0_NMI_MASK:
        s->proc0_nmi_mask =
            rp2040_syscfg_apply_alias(s->proc0_nmi_mask, value, alias);
        rp2040_syscfg_update(s);
        break;
    case SYSCFG_PROC1_NMI_MASK:
        s->proc1_nmi_mask =
            rp2040_syscfg_apply_alias(s->proc1_nmi_mask, value, alias);
        rp2040_syscfg_update(s);
        break;
    case SYSCFG_PROC_CONFIG:
        s->proc_config =
            rp2040_syscfg_apply_alias(s->proc_config, value, alias) &
            PROC_CONFIG_RW_MASK;
        break;
    case SYSCFG_PROC_IN_SYNC_BYPASS:
        s->proc_in_sync_bypass =
            rp2040_syscfg_apply_alias(s->proc_in_sync_bypass, value, alias) &
            PROC_IN_SYNC_BYPASS_MASK;
        break;
    case SYSCFG_PROC_IN_SYNC_BYPASS_HI:
        s->proc_in_sync_bypass_hi =
            rp2040_syscfg_apply_alias(s->proc_in_sync_bypass_hi, value,
                                      alias) &
            PROC_IN_SYNC_BYPASS_HI_MASK;
        break;
    case SYSCFG_DBGFORCE:
        s->dbgforce =
            rp2040_syscfg_apply_alias(s->dbgforce, value, alias) &
            DBGFORCE_RW_MASK;
        break;
    case SYSCFG_MEMPOWERDOWN:
        s->mempowerdown =
            rp2040_syscfg_apply_alias(s->mempowerdown, value, alias) &
            MEMPOWERDOWN_MASK;
        rp2040_syscfg_update(s);
        break;
    default:
        rp2040_log_unimplemented_write("syscfg", size,
                                       RP2040_SYSCFG_BASE + addr, offset,
                                       value64);
        break;
    }
}

static const MemoryRegionOps rp2040_syscfg_ops = {
    .read = rp2040_syscfg_read,
    .write = rp2040_syscfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_syscfg_reset(DeviceState *dev)
{
    RP2040SysCfgState *s = RP2040_SYSCFG(dev);

    s->proc0_nmi_mask = 0;
    s->proc1_nmi_mask = 0;
    s->proc_config = PROC_CONFIG_RESET;
    s->proc_in_sync_bypass = 0;
    s->proc_in_sync_bypass_hi = 0;
    s->dbgforce = DBGFORCE_RESET;
    s->mempowerdown = 0;
    rp2040_syscfg_update(s);
}

static int rp2040_syscfg_post_load(void *opaque, int version_id)
{
    RP2040SysCfgState *s = opaque;

    rp2040_syscfg_update(s);
    return 0;
}

static void rp2040_syscfg_init(Object *obj)
{
    RP2040SysCfgState *s = RP2040_SYSCFG(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_syscfg_ops, s,
                          "rp2040.syscfg", RP2040_SYSCFG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_syscfg_vmstate = {
    .name = TYPE_RP2040_SYSCFG,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = rp2040_syscfg_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(proc0_nmi_mask, RP2040SysCfgState),
        VMSTATE_UINT32(proc1_nmi_mask, RP2040SysCfgState),
        VMSTATE_UINT32(proc_config, RP2040SysCfgState),
        VMSTATE_UINT32(proc_in_sync_bypass, RP2040SysCfgState),
        VMSTATE_UINT32(proc_in_sync_bypass_hi, RP2040SysCfgState),
        VMSTATE_UINT32(dbgforce, RP2040SysCfgState),
        VMSTATE_UINT32(mempowerdown, RP2040SysCfgState),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_syscfg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_syscfg_reset);
    dc->vmsd = &rp2040_syscfg_vmstate;
}

static const TypeInfo rp2040_syscfg_info = {
    .name          = TYPE_RP2040_SYSCFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040SysCfgState),
    .instance_init = rp2040_syscfg_init,
    .class_init    = rp2040_syscfg_class_init,
};

static void rp2040_syscfg_register_types(void)
{
    type_register_static(&rp2040_syscfg_info);
}
type_init(rp2040_syscfg_register_types)
