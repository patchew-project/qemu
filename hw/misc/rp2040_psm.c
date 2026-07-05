/*
 * RP2040 power-on state machine emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_psm.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define PSM_FRCE_ON   0x00
#define PSM_FRCE_OFF  0x04
#define PSM_WDSEL     0x08
#define PSM_DONE      0x0c

#define ATOMIC_ALIAS_MASK 0x3000
#define ATOMIC_XOR        0x1000
#define ATOMIC_SET        0x2000
#define ATOMIC_CLR        0x3000

static uint32_t rp2040_psm_apply_alias(uint32_t old, uint32_t value,
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

static void rp2040_psm_update(RP2040PsmState *s)
{
    if (s->update) {
        s->update(s->update_opaque);
    }
}

void rp2040_psm_set_update_callback(RP2040PsmState *s,
                                    RP2040PsmUpdateFn update,
                                    void *opaque)
{
    s->update = update;
    s->update_opaque = opaque;
}

uint32_t rp2040_psm_get_frce_off(RP2040PsmState *s)
{
    return s->frce_off;
}

static uint32_t rp2040_psm_done(RP2040PsmState *s)
{
    return RP2040_PSM_VALID_MASK & ~s->frce_off;
}

static uint64_t rp2040_psm_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040PsmState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    switch (offset) {
    case PSM_FRCE_ON:
        value = s->frce_on;
        break;
    case PSM_FRCE_OFF:
        value = s->frce_off;
        break;
    case PSM_WDSEL:
        value = s->wdsel;
        break;
    case PSM_DONE:
        value = rp2040_psm_done(s);
        break;
    default:
        value = 0;
        rp2040_log_unimplemented_read("psm", size,
                                      RP2040_PSM_BASE + addr, offset, value);
        break;
    }

    return value;
}

static void rp2040_psm_write(void *opaque, hwaddr addr,
                             uint64_t value64, unsigned size)
{
    RP2040PsmState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t value = value64;
    uint32_t old_frce_off = s->frce_off;

    switch (offset) {
    case PSM_FRCE_ON:
        s->frce_on = rp2040_psm_apply_alias(s->frce_on, value, alias) &
                     RP2040_PSM_VALID_MASK;
        break;
    case PSM_FRCE_OFF:
        s->frce_off = rp2040_psm_apply_alias(s->frce_off, value, alias) &
                      RP2040_PSM_VALID_MASK;
        if (s->frce_off != old_frce_off) {
            rp2040_psm_update(s);
        }
        break;
    case PSM_WDSEL:
        s->wdsel = rp2040_psm_apply_alias(s->wdsel, value, alias) &
                   RP2040_PSM_VALID_MASK;
        break;
    case PSM_DONE:
        break;
    default:
        rp2040_log_unimplemented_write("psm", size,
                                       RP2040_PSM_BASE + addr, offset,
                                       value64);
        break;
    }
}

static const MemoryRegionOps rp2040_psm_ops = {
    .read = rp2040_psm_read,
    .write = rp2040_psm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_psm_reset(DeviceState *dev)
{
    RP2040PsmState *s = RP2040_PSM(dev);

    s->frce_on = 0;
    s->frce_off = 0;
    s->wdsel = 0;
    rp2040_psm_update(s);
}

static int rp2040_psm_post_load(void *opaque, int version_id)
{
    RP2040PsmState *s = opaque;

    rp2040_psm_update(s);
    return 0;
}

static void rp2040_psm_init(Object *obj)
{
    RP2040PsmState *s = RP2040_PSM(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_psm_ops, s,
                          "rp2040.psm", RP2040_PSM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_psm_vmstate = {
    .name = TYPE_RP2040_PSM,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = rp2040_psm_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(frce_on, RP2040PsmState),
        VMSTATE_UINT32(frce_off, RP2040PsmState),
        VMSTATE_UINT32(wdsel, RP2040PsmState),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_psm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_psm_reset);
    dc->vmsd = &rp2040_psm_vmstate;
}

static const TypeInfo rp2040_psm_info = {
    .name          = TYPE_RP2040_PSM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040PsmState),
    .instance_init = rp2040_psm_init,
    .class_init    = rp2040_psm_class_init,
};

static void rp2040_psm_register_types(void)
{
    type_register_static(&rp2040_psm_info);
}
type_init(rp2040_psm_register_types)
