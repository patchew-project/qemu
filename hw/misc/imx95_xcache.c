/*
 * NXP i.MX 95 XCACHE controller stub model
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Minimal model of the M33 XCACHE controller (the two instances at
 * 0x44400000 "PC" and 0x44400800 "PS"). The NXP System Manager firmware
 * enables and invalidates its caches early in init by writing the cache
 * control register (CCR): it sets ENCACHE plus the invalidate/push
 * command bits and the GO bit, then polls.
 *
 * QEMU has no cache to model, so the only behaviour that matters is the
 * self-clearing of the command bits: real hardware clears GO and the
 * INVWn/PUSHWn bits once the (instantaneous, for us) operation completes,
 * and leaves ENCACHE/config bits set. A plain RAM stub would leave GO set
 * forever (the SM's "wait for GO to clear" loop would hang); a zero-return
 * stub would leave ENCACHE clear (an "is cache enabled" check would hang).
 * So CCR persists everything except the command bits, which read back 0.
 * The line-maintenance registers (CLCR/CSAR/CCVR) are plain storage.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TYPE_IMX95_XCACHE "imx95.xcache"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95XCacheState, IMX95_XCACHE)

/* Each instance owns 0x800 (the two M33 instances are 0x800 apart). */
#define IMX95_XCACHE_REG_SIZE   0x800

#define XCACHE_CCR              0x00
#define XCACHE_CLCR             0x04
#define XCACHE_CSAR             0x08
#define XCACHE_CCVR             0x0C

/* CCR bits that self-clear when the cache command completes. */
#define CCR_GO                  0x80000000u
#define CCR_PUSHW1              0x08000000u
#define CCR_INVW1               0x04000000u
#define CCR_PUSHW0              0x02000000u
#define CCR_INVW0               0x01000000u
#define CCR_CMD_BITS \
    (CCR_GO | CCR_PUSHW1 | CCR_INVW1 | CCR_PUSHW0 | CCR_INVW0)

struct IMX95XCacheState {
    SysBusDevice    parent_obj;
    MemoryRegion    iomem;

    uint32_t        ccr;
    uint32_t        clcr;
    uint32_t        csar;
    uint32_t        ccvr;
};

static uint64_t imx95_xcache_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX95XCacheState *s = opaque;

    trace_imx95_xcache_read(offset);

    switch (offset) {
    case XCACHE_CCR:
        return s->ccr;
    case XCACHE_CLCR:
        return s->clcr;
    case XCACHE_CSAR:
        return s->csar;
    case XCACHE_CCVR:
        return s->ccvr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void imx95_xcache_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    IMX95XCacheState *s = opaque;

    trace_imx95_xcache_write(offset, value);

    switch (offset) {
    case XCACHE_CCR:
        /* Command bits complete instantly: store everything else. */
        s->ccr = (uint32_t)value & ~CCR_CMD_BITS;
        break;
    case XCACHE_CLCR:
        s->clcr = value;
        break;
    case XCACHE_CSAR:
        s->csar = value;
        break;
    case XCACHE_CCVR:
        s->ccvr = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx
                      " value 0x%" PRIx64 "\n",
                      __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps imx95_xcache_ops = {
    .read = imx95_xcache_read,
    .write = imx95_xcache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx95_xcache_reset_hold(Object *obj, ResetType type)
{
    IMX95XCacheState *s = IMX95_XCACHE(obj);

    s->ccr = 0;
    s->clcr = 0;
    s->csar = 0;
    s->ccvr = 0;
}

static void imx95_xcache_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMX95XCacheState *s = IMX95_XCACHE(obj);

    memory_region_init_io(&s->iomem, obj, &imx95_xcache_ops, s,
                          TYPE_IMX95_XCACHE, IMX95_XCACHE_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_imx95_xcache = {
    .name = TYPE_IMX95_XCACHE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ccr, IMX95XCacheState),
        VMSTATE_UINT32(clcr, IMX95XCacheState),
        VMSTATE_UINT32(csar, IMX95XCacheState),
        VMSTATE_UINT32(ccvr, IMX95XCacheState),
        VMSTATE_END_OF_LIST()
    },
};

static void imx95_xcache_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_imx95_xcache;
    rc->phases.hold = imx95_xcache_reset_hold;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NXP i.MX 95 XCACHE controller (stub)";
}

static const TypeInfo imx95_xcache_info = {
    .name           = TYPE_IMX95_XCACHE,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMX95XCacheState),
    .instance_init  = imx95_xcache_init,
    .class_init     = imx95_xcache_class_init,
};

static void imx95_xcache_register_types(void)
{
    type_register_static(&imx95_xcache_info);
}

type_init(imx95_xcache_register_types)
