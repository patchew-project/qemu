/*
 * ARM GICv5 emulation: Interrupt Routing Service (IRS)
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/intc/arm_gicv5.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "trace.h"

OBJECT_DEFINE_TYPE(GICv5, gicv5, ARM_GICV5, ARM_GICV5_COMMON)

static const char *domain_name[] = {
    [GICV5_ID_S] = "Secure",
    [GICV5_ID_NS] = "NonSecure",
    [GICV5_ID_EL3] = "EL3",
    [GICV5_ID_REALM] = "Realm",
};

static bool config_readl(GICv5 *s, GICv5Domain domain, hwaddr offset,
                         uint64_t *data, MemTxAttrs attrs)
{
    return false;
}

static bool config_writel(GICv5 *s, GICv5Domain domain, hwaddr offset,
                          uint64_t data, MemTxAttrs attrs)
{
    return false;
}

static bool config_readll(GICv5 *s, GICv5Domain domain, hwaddr offset,
                          uint64_t *data, MemTxAttrs attrs)
{
    return false;
}

static bool config_writell(GICv5 *s, GICv5Domain domain, hwaddr offset,
                           uint64_t data, MemTxAttrs attrs)
{
    return false;
}

static MemTxResult config_read(void *opaque, GICv5Domain domain, hwaddr offset,
                               uint64_t *data, unsigned size,
                               MemTxAttrs attrs)
{
    GICv5 *s = ARM_GICV5(opaque);
    bool result;

    switch (size) {
    case 4:
        result = config_readl(s, domain, offset, data, attrs);
        break;
    case 8:
        result = config_readll(s, domain, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read for IRS %s config frame "
                      "at offset " HWADDR_FMT_plx
                      " size %u\n", __func__, domain_name[domain],
                      offset, size);
        trace_gicv5_badread(domain_name[domain], offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so we log the error but return MEMTX_OK so we don't cause
         * a spurious data abort.
         */
        *data = 0;
    } else {
        trace_gicv5_read(domain_name[domain], offset, *data, size);
    }

    return MEMTX_OK;
}

static MemTxResult config_write(void *opaque, GICv5Domain domain,
                                hwaddr offset, uint64_t data, unsigned size,
                                MemTxAttrs attrs)
{
    GICv5 *s = ARM_GICV5(opaque);
    bool result;

    switch (size) {
    case 4:
        result = config_writel(s, domain, offset, data, attrs);
        break;
    case 8:
        result = config_writell(s, domain, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write for IRS %s config frame "
                      "at offset " HWADDR_FMT_plx
                      " size %u\n", __func__, domain_name[domain],
                      offset, size);
        trace_gicv5_badwrite(domain_name[domain], offset, data, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so we log the error but return MEMTX_OK so we don't cause
         * a spurious data abort.
         */
    } else {
        trace_gicv5_write(domain_name[domain], offset, data, size);
    }

    return MEMTX_OK;
}

#define DEFINE_READ_WRITE_WRAPPERS(NAME, DOMAIN)                           \
    static MemTxResult config_##NAME##_read(void *opaque, hwaddr offset,   \
                                            uint64_t *data, unsigned size, \
                                            MemTxAttrs attrs)              \
    {                                                                      \
        return config_read(opaque, DOMAIN, offset, data, size, attrs);     \
    }                                                                      \
    static MemTxResult config_##NAME##_write(void *opaque, hwaddr offset,  \
                                             uint64_t data, unsigned size, \
                                             MemTxAttrs attrs)             \
    {                                                                      \
        return config_write(opaque, DOMAIN, offset, data, size, attrs);    \
    }

DEFINE_READ_WRITE_WRAPPERS(ns, GICV5_ID_NS)
DEFINE_READ_WRITE_WRAPPERS(realm, GICV5_ID_REALM)
DEFINE_READ_WRITE_WRAPPERS(secure, GICV5_ID_S)
DEFINE_READ_WRITE_WRAPPERS(el3, GICV5_ID_EL3)

static const MemoryRegionOps config_frame_ops[NUM_GICV5_DOMAINS] = {
    [GICV5_ID_S] = {
        .read_with_attrs = config_secure_read,
        .write_with_attrs = config_secure_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.min_access_size = 4,
        .valid.max_access_size = 8,
        .impl.min_access_size = 4,
        .impl.max_access_size = 8,
    },
    [GICV5_ID_NS] = {
        .read_with_attrs = config_ns_read,
        .write_with_attrs = config_ns_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.min_access_size = 4,
        .valid.max_access_size = 8,
        .impl.min_access_size = 4,
        .impl.max_access_size = 8,
    },
    [GICV5_ID_EL3] = {
        .read_with_attrs = config_el3_read,
        .write_with_attrs = config_el3_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.min_access_size = 4,
        .valid.max_access_size = 8,
        .impl.min_access_size = 4,
        .impl.max_access_size = 8,
    },
    [GICV5_ID_REALM] = {
        .read_with_attrs = config_realm_read,
        .write_with_attrs = config_realm_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid.min_access_size = 4,
        .valid.max_access_size = 8,
        .impl.min_access_size = 4,
        .impl.max_access_size = 8,
    },
};

static void gicv5_reset_hold(Object *obj, ResetType type)
{
    GICv5 *s = ARM_GICV5(obj);
    GICv5Class *c = ARM_GICV5_GET_CLASS(s);

    if (c->parent_phases.hold) {
        c->parent_phases.hold(obj, type);
    }
}

static void gicv5_realize(DeviceState *dev, Error **errp)
{
    GICv5Common *cs = ARM_GICV5_COMMON(dev);
    GICv5Class *gc = ARM_GICV5_GET_CLASS(dev);

    ERRP_GUARD();

    gc->parent_realize(dev, errp);
    if (*errp) {
        return;
    }

    /*
     * When we implement support for more than one interrupt domain,
     * we will provide some QOM properties so the board can configure
     * which domains are implemented. For now, we only implement the
     * NS domain.
     */
    cs->implemented_domains = (1 << GICV5_ID_NS);
    gicv5_common_init_irqs_and_mmio(cs, config_frame_ops);
}

static void gicv5_init(Object *obj)
{
}

static void gicv5_finalize(Object *obj)
{
}

static void gicv5_class_init(ObjectClass *oc, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    GICv5Class *gc = ARM_GICV5_CLASS(oc);

    device_class_set_parent_realize(dc, gicv5_realize, &gc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, gicv5_reset_hold, NULL,
                                       &gc->parent_phases);
}
