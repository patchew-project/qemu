/*
 * NXP i.MX Messaging Unit (MU)
 *
 * Copyright (c) 2026, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "qemu/log.h"

#include "hw/misc/imx8mp_mu.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/resettable.h"
#include "migration/vmstate.h"


#define MU_SR_RFn_V1(x)   (1u << (24 + (3 - (x))))
#define MU_SR_TEn_V1(x)   (1u << (20 + (3 - (x))))
#define MU_SR_GIPn_V1(x)  (1u << (28 + (3 - (x))))
#define MU_CR_RIEn_V1(x)  (1u << (24 + (3 - (x))))
#define MU_CR_TIEn_V1(x)  (1u << (20 + (3 - (x))))
#define MU_CR_GIEn_V1(x)  (1u << (28 + (3 - (x))))
#define MU_CR_GIRn_V1(x)  (1u << (16 + (3 - (x))))


static inline IMX8MPMUState *imx8mp_mu_peer(IMX8MPMUState *s)
{
    return s->peer;
}

static void imx8mp_mu_update_irq(IMX8MPMUState *s)
{

    bool pending = false;
    uint32_t sr = s->mu[MU_SR];
    uint32_t cr = s->mu[MU_CR];

    for (int i = 0; i < 4; i++) {
        /* TX done interrupt (TEn + TIEn) */
        if ((sr & MU_SR_TEn_V1(i)) && (cr & MU_CR_TIEn_V1(i))) {
            pending = true;
            break;
         }

        /* RX data interrupt (RFn + RIEn) */
        if ((sr & MU_SR_RFn_V1(i)) && (cr & MU_CR_RIEn_V1(i))) {
            pending = true;
            break;
        }

        /* Doorbell interrupt (GIPn + GIEn) - keep for completeness */
        if ((sr & MU_SR_GIPn_V1(i)) && (cr & MU_CR_GIEn_V1(i))) {
            pending = true;
            break;
        }
    }
    qemu_set_irq(s->irq, pending);
}

static uint64_t imx8mp_mu_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX8MPMUState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx8mp_mu: invalid read size %u @0x%" HWADDR_PRIx "\n",
                      size, offset);
        return 0;
    }

    if (offset & 3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx8mp_mu: unaligned read @0x%"
              HWADDR_PRIx "\n", offset);
        return 0;
    }

    if (offset < MU_RR0) {
        unsigned tr = offset >> 2;
        if (s->strict_access) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx8mp_mu: strict: read from TR%u"
              " @0x%" HWADDR_PRIx "\n", tr, offset);
            return 0;
        }
        return s->mu[MU_TR0 + tr];
    }

    if (offset >= MU_RR0 && offset < MU_SR) {
        unsigned rr = (offset - MU_RR0) >> 2;
        uint32_t v = s->mu[MU_RR0 + rr];
    IMX8MPMUState *p = imx8mp_mu_peer(s);

        if (s->strict_access && !(s->mu[MU_SR] & MU_SR_RFn_V1(rr))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx8mp_mu: strict: read from empty RR%u"
              " @0x%" HWADDR_PRIx "\n", rr, offset);
            return 0;
        }

    if (s->mu[MU_SR] & MU_SR_RFn_V1(rr)) {
            s->mu[MU_SR] &= ~MU_SR_RFn_V1(rr);
            if (p) {
                p->mu[MU_SR] |= MU_SR_TEn_V1(rr);
                imx8mp_mu_update_irq(p);
            }
        }
        imx8mp_mu_update_irq(s);
        return v;
    }

    switch (offset) {
    case MU_SR:
        return s->mu[MU_SR];
    case MU_CR:
        return s->mu[MU_CR];
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx8mp_mu: bad read @0x%" HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void imx8mp_mu_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    IMX8MPMUState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx8mp_mu: invalid write size %u @0x%" HWADDR_PRIx "\n",
                      size, offset);
        return;
    }

    if (offset & 3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx8mp_mu: unaligned write @0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }

    /* TR0..TR3 */
    if (offset < MU_RR0) {
        unsigned tr = offset >> 2;
        uint32_t v = (uint32_t)value;
        IMX8MPMUState *p = imx8mp_mu_peer(s);

    if (s->strict_access && !(s->mu[MU_SR] & MU_SR_TEn_V1(tr))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "imx8mp_mu: strict: write to non-empty"
                          " TR%u @0x%" HWADDR_PRIx "\n",
                          tr, offset);
            return;
        }

        s->mu[MU_TR0 + tr] = v;

        /*
         * Writing TR consumes local TX-empty (TE=0) and delivers the word
         * into peer RR while setting peer RX-full (RF=1).
         *
         */
        s->mu[MU_SR] &= ~MU_SR_TEn_V1(tr); /* TE=0 */
        if (p) {
            p->mu[MU_RR0 + tr] = v;
            p->mu[MU_SR] |= MU_SR_RFn_V1(tr); /* RF=1 */
            imx8mp_mu_update_irq(p);
        }

        imx8mp_mu_update_irq(s);
        return;
    }

    /* RR are read-only from this side */
    if (offset >= MU_RR0 && offset < MU_SR) {
        unsigned rr = (offset - MU_RR0) >> 2;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx8mp_mu: write to RR%d ignored\n", rr);
        return;
    }

    switch (offset) {
    case MU_CR: {
        uint32_t v = (uint32_t)value;
        IMX8MPMUState *p = imx8mp_mu_peer(s);
        uint32_t gir_mask = 0;

        for (int i = 0; i < 4; i++) {
            gir_mask |= MU_CR_GIRn_V1(i);
        }

        if (p) {
            for (int i = 0; i < 4; i++) {
                if (v & MU_CR_GIRn_V1(i)) {
                    /* Set peer doorbell pending */
                    p->mu[MU_SR] |= MU_SR_GIPn_V1(i);
                }
            }
            imx8mp_mu_update_irq(p);
        }

        /*
         * Model GIRn as write-to-trigger (self-clearing) and keep other CR
         * bits latched.
         */
        v &= ~gir_mask;

        s->mu[MU_CR] = v;
        imx8mp_mu_update_irq(s);
        return;
    }

    case MU_SR:{

        /* Write-1-to-clear for doorbell bits */
        uint32_t v = (uint32_t)value;
        for (int i = 0; i < 4; i++) {
            if (v & MU_SR_GIPn_V1(i)) {
                s->mu[MU_SR] &= ~MU_SR_GIPn_V1(i);
            }
        }
        imx8mp_mu_update_irq(s);
        return;
    }

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "imx8mp_mu: bad write @0x%" HWADDR_PRIx
              " = 0x%08" PRIx64 "\n", offset, value);
        return;
    }
}

static ResettablePhases imx8mp_mu_parent_phases;

static void imx8mp_mu_reset_enter(Object *obj, ResetType type)
{
    IMX8MPMUState *s = IMX8MP_MU(obj);

    if (imx8mp_mu_parent_phases.enter) {
        imx8mp_mu_parent_phases.enter(obj, type);
    }
    memset(s->mu, 0, sizeof(s->mu));

    s->mu[MU_SR] = 0x00F00080;
}

static void imx8mp_mu_reset_hold(Object *obj, ResetType type)
{
    IMX8MPMUState *s = IMX8MP_MU(obj);
    if (imx8mp_mu_parent_phases.hold) {
        imx8mp_mu_parent_phases.hold(obj, type);
    }

    imx8mp_mu_update_irq(s);
}

static const MemoryRegionOps imx8mp_mu_ops = {
    .read = imx8mp_mu_read,
    .write = imx8mp_mu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    .unaligned = false,
    },
};

static const VMStateDescription imx8mp_mu_vmstate = {
    .name = TYPE_IMX8MP_MU,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
    VMSTATE_UINT32_ARRAY(mu, IMX8MPMUState, MU_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx8mp_mu_init(Object *obj)
{
    IMX8MPMUState *s = IMX8MP_MU(obj);
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);

    /* Default to strict hardware-like access semantics. */
    s->strict_access = true;

    memory_region_init(&s->mmio.container, obj, TYPE_IMX8MP_MU,
                       IMX8MP_MU_MMIO_SIZE);

    /* Implemented subset as an IO subregion at offset 0 */
    memory_region_init_io(&s->mmio.regs, obj, &imx8mp_mu_ops, s,
                          TYPE_IMX8MP_MU ".regs", sizeof(s->mu));
    memory_region_add_subregion(&s->mmio.container, 0, &s->mmio.regs);

    sysbus_init_mmio(sd, &s->mmio.container);
    sysbus_init_irq(sd, &s->irq);
}

static void imx8mp_mu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    resettable_class_set_parent_phases(rc,
                                       imx8mp_mu_reset_enter,
                                       imx8mp_mu_reset_hold,
                                       NULL,
                                       &imx8mp_mu_parent_phases);
    dc->vmsd = &imx8mp_mu_vmstate;
    dc->desc  = "i.MX 8M Plus Messaging Unit";
}

static const TypeInfo imx8mp_mu_type_info[] = {
    {
        .name = TYPE_IMX8MP_MU,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(IMX8MPMUState),
        .instance_init = imx8mp_mu_init,
        .class_init = imx8mp_mu_class_init,
    }
};

DEFINE_TYPES(imx8mp_mu_type_info);
