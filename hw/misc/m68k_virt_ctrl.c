/*
 * SPDX-License-Identifer: GPL-2.0-or-later
 *
 * Virt m68k system Controller
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"
#include "sysemu/runstate.h"
#include "hw/misc/m68k_virt_ctrl.h"

enum {
    REG_FEATURES = 0x00,
    REG_CMD      = 0x04,
};

#define FEAT_POWER_CTRL 0x00000001

enum {
    CMD_NOOP,
    CMD_RESET,
    CMD_HALT,
    CMD_PANIC,
};

static uint64_t m68k_virt_ctrl_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    M68KVirtCtrlState *s = opaque;
    uint64_t value = 0;

    switch (addr) {
    case REG_FEATURES:
        value = FEAT_POWER_CTRL;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register read 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }

    trace_m68k_virt_ctrl_write(s, addr, size, value);

    return value;
}

static void m68k_virt_ctrl_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    M68KVirtCtrlState *s = opaque;

    trace_m68k_virt_ctrl_write(s, addr, size, value);

    switch (addr) {
    case REG_CMD:
        switch (value) {
        case CMD_NOOP:
            break;
        case CMD_RESET:
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            break;
        case CMD_HALT:
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            break;
        case CMD_PANIC:
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_PANIC);
            break;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register write 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps m68k_virt_ctrl_ops = {
    .read = m68k_virt_ctrl_read,
    .write = m68k_virt_ctrl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.max_access_size = 4,
    .impl.max_access_size = 4,
};

static void m68k_virt_ctrl_reset(DeviceState *dev)
{
    M68KVirtCtrlState *s = M68K_VIRT_CTRL(dev);

    trace_m68k_virt_ctrl_reset(s);
}

static void m68k_virt_ctrl_realize(DeviceState *dev, Error **errp)
{
    M68KVirtCtrlState *s = M68K_VIRT_CTRL(dev);

    trace_m68k_virt_ctrl_instance_init(s);

    memory_region_init_io(&s->iomem, OBJECT(s), &m68k_virt_ctrl_ops, s,
                          "m68k-virt-ctrl", 0x100);
}

static const VMStateDescription vmstate_m68k_virt_ctrl = {
    .name = "m68k-virt-ctrl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(irq_enabled, M68KVirtCtrlState),
        VMSTATE_END_OF_LIST()
    }
};

static void m68k_virt_ctrl_instance_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    M68KVirtCtrlState *s = M68K_VIRT_CTRL(obj);

    trace_m68k_virt_ctrl_instance_init(s);

    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);
}

static void m68k_virt_ctrl_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = m68k_virt_ctrl_reset;
    dc->realize = m68k_virt_ctrl_realize;
    dc->vmsd = &vmstate_m68k_virt_ctrl;
}

static const TypeInfo m68k_virt_ctrl_info = {
    .name = TYPE_M68K_VIRT_CTRL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = m68k_virt_ctrl_class_init,
    .instance_init = m68k_virt_ctrl_instance_init,
    .instance_size = sizeof(M68KVirtCtrlState),
};

static void m68k_virt_ctrl_register_types(void)
{
    type_register_static(&m68k_virt_ctrl_info);
}

type_init(m68k_virt_ctrl_register_types)
