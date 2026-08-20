/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * TI K3 Global Timebase Counter (GTC) register stub.
 *
 * The GTC distributes a system counter and its nominal frequency to the ARM
 * generic timers. Only CNTCR and CNTFID0 are implemented; the counter itself
 * is not modelled. Reset reports an enabled 200 MHz counter, the AM64x
 * default.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/ti-k3-gtc.h"

#define TI_K3_GTC_SIZE       0x1000

#define GTC_CNTCR            0x000   /* control; bit0 = EN */
#define GTC_CNTFID0          0x020   /* frequency id 0 (Hz) */

static uint64_t ti_k3_gtc_read(void *opaque, hwaddr addr, unsigned size)
{
    TIK3GtcState *s = TI_K3_GTC(opaque);

    switch (addr) {
    case GTC_CNTCR:
        return s->cntcr;
    case GTC_CNTFID0:
        return s->cntfid0;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented read @0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }
}

static void ti_k3_gtc_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned size)
{
    TIK3GtcState *s = TI_K3_GTC(opaque);

    switch (addr) {
    case GTC_CNTCR:
        s->cntcr = (uint32_t)val;
        break;
    case GTC_CNTFID0:
        s->cntfid0 = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented write @0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps ti_k3_gtc_ops = {
    .read = ti_k3_gtc_read,
    .write = ti_k3_gtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void ti_k3_gtc_init(Object *obj)
{
    TIK3GtcState *s = TI_K3_GTC(obj);

    memory_region_init_io(&s->iomem, obj, &ti_k3_gtc_ops, s,
                          TYPE_TI_K3_GTC, TI_K3_GTC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const Property ti_k3_gtc_properties[] = {
    DEFINE_PROP_UINT32("cntcr", TIK3GtcState, cntcr, 0x1),
    DEFINE_PROP_UINT32("cntfid0", TIK3GtcState, cntfid0, 200000000),
};

static void ti_k3_gtc_class_init(ObjectClass *klass, const void *data)
{
    device_class_set_props(DEVICE_CLASS(klass), ti_k3_gtc_properties);
}

static const TypeInfo ti_k3_gtc_info = {
    .name = TYPE_TI_K3_GTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIK3GtcState),
    .instance_init = ti_k3_gtc_init,
    .class_init = ti_k3_gtc_class_init,
};

static void ti_k3_gtc_register_types(void)
{
    type_register_static(&ti_k3_gtc_info);
}

type_init(ti_k3_gtc_register_types)
