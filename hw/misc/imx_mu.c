/*
 * NXP i.MX Messaging Unit (MU) device model - V2 register layout
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements the subset of the V2 MU register set exercised by U-Boot
 * SPL's imx_mu_init_generic() probe path and by SCMI mailbox traffic:
 * CR/SR/GCR/GSR/GIER/TCR/TSR/RCR/RSR + 4 TR/RR data registers, plus
 * a doorbell mechanism via GCR.GIRn / GSR.GIPn.
 *
 * Models enough for the SPL probe to succeed and for the SM to
 * handshake on doorbells. No
 * cross-domain interrupt routing, no flag-bit semantics beyond
 * read/write-back, no per-channel TX/RX FIFO depth modelling.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/imx_mu.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

static void imx_mu_update_irq(IMXMUState *s);

static int imx_mu_post_load(void *opaque, int version_id)
{
    /* Recompute the IRQ line level from the restored register state. */
    imx_mu_update_irq(opaque);
    return 0;
}

static const VMStateDescription vmstate_imx_mu = {
    .name = TYPE_IMX_MU,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = imx_mu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cr, IMXMUState),
        VMSTATE_UINT32(sr, IMXMUState),
        VMSTATE_UINT32(fcr, IMXMUState),
        VMSTATE_UINT32(fsr, IMXMUState),
        VMSTATE_UINT32(gier, IMXMUState),
        VMSTATE_UINT32(gcr, IMXMUState),
        VMSTATE_UINT32(gsr, IMXMUState),
        VMSTATE_UINT32(tcr, IMXMUState),
        VMSTATE_UINT32(tsr, IMXMUState),
        VMSTATE_UINT32(rcr, IMXMUState),
        VMSTATE_UINT32(rsr, IMXMUState),
        VMSTATE_UINT32_ARRAY(tr, IMXMUState, IMX_MU_NUM_CHANNELS),
        VMSTATE_UINT32_ARRAY(rr, IMXMUState, IMX_MU_NUM_CHANNELS),
        VMSTATE_END_OF_LIST()
    },
};

/*
 * Recompute the IRQ line. V2 layout: each xSR pending bit ANDed with
 * the matching xCR enable bit; any one of those produces an interrupt.
 * GP-IRQ comes from (GSR.GIPn & GIER.GIEn) across the channels in use.
 */
static void imx_mu_update_irq(IMXMUState *s)
{
    uint32_t mask = (1u << IMX_MU_NUM_CHANNELS) - 1u;
    bool level =
        ((s->gsr & s->gier & mask) != 0) ||
        ((s->tsr & s->tcr  & mask) != 0) ||
        ((s->rsr & s->rcr  & mask) != 0);

    qemu_set_irq(s->irq, level);
}

static void imx_mu_reset_state(IMXMUState *s)
{
    s->cr   = 0;
    s->sr   = 0;
    s->fcr  = 0;
    s->fsr  = 0;
    s->gier = 0;
    s->gcr  = 0;
    s->gsr  = 0;
    s->tcr  = 0;
    /* All TX slots come up empty - TEn bits set. */
    s->tsr  = (1u << IMX_MU_NUM_CHANNELS) - 1u;
    s->rcr  = 0;
    s->rsr  = 0;
    memset(s->tr, 0, sizeof(s->tr));
    memset(s->rr, 0, sizeof(s->rr));
}

static void imx_mu_reset_at_boot_hold(Object *obj, ResetType type)
{
    IMXMUState *s = IMX_MU(obj);

    imx_mu_reset_state(s);
    imx_mu_update_irq(s);
}

void imx_mu_assert_gip(IMXMUState *s, unsigned int idx)
{
    if (idx >= IMX_MU_NUM_CHANNELS) {
        return;
    }
    trace_imx_mu_gip(idx);
    s->gsr |= IMX_MU_V2_BIT(idx);
    imx_mu_update_irq(s);
}

void imx_mu_set_doorbell_handler(IMXMUState *s,
                                 IMXMUDoorbellHandler handler,
                                 void *opaque)
{
    s->doorbell_handler = handler;
    s->doorbell_opaque  = opaque;
}

void imx_mu_set_tr_write_handler(IMXMUState *s,
                                 IMXMUTRWriteHandler handler,
                                 void *opaque)
{
    s->tr_write_handler = handler;
    s->tr_write_opaque  = opaque;
}

void imx_mu_deliver_rr(IMXMUState *s, unsigned int idx, uint32_t value)
{
    if (idx >= IMX_MU_NUM_CHANNELS) {
        return;
    }
    trace_imx_mu_rr_deliver(idx, value);
    s->rr[idx]  = value;
    s->rsr     |= IMX_MU_V2_BIT(idx);
    imx_mu_update_irq(s);
}

static uint64_t imx_mu_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXMUState *s = opaque;
    uint64_t value = 0;

    if (offset >= IMX_MU_TR_BASE &&
        offset < IMX_MU_TR_BASE + IMX_MU_NUM_CHANNELS * 4) {
        value = s->tr[(offset - IMX_MU_TR_BASE) / 4];
        return value;
    }
    if (offset >= IMX_MU_RR_BASE &&
        offset < IMX_MU_RR_BASE + IMX_MU_NUM_CHANNELS * 4) {
        unsigned idx = (offset - IMX_MU_RR_BASE) / 4;
        value = s->rr[idx];
        /*
         * Reading RR[n] clears RSR.RFn (the receive-full status bit)
         * so the next sender can re-queue. The SCMI transport doesn't
         * rely on this, but it matches the documented HW
         * behaviour and lets the U-Boot probe drain pending data.
         *
         * For a peer-linked MU this completes the TX handshake: the word
         * was delivered into RR[n] by the peer's TR[n] write (which left
         * the peer's TSR.TEn clear), so draining it re-asserts the peer's
         * TX-empty and lets a blocked mbox send finish.
         */
        s->rsr &= ~IMX_MU_V2_BIT(idx);
        if (s->peer) {
            s->peer->tsr |= IMX_MU_V2_BIT(idx);
            imx_mu_update_irq(s->peer);
        }
        imx_mu_update_irq(s);
        return value;
    }

    switch (offset) {
    case IMX_MU_VER:
        /* Plausible V2 version. Linux/U-Boot do not branch on this. */
        value = 0x00020000;
        break;
    case IMX_MU_PAR:
        /* TR count in [7:0], RR count in [15:8] (V2 encoding). */
        value = ((uint32_t)IMX_MU_NUM_CHANNELS) |
                ((uint32_t)IMX_MU_NUM_CHANNELS << 8);
        break;
    case IMX_MU_CR:
        value = s->cr;
        break;
    case IMX_MU_SR:
        value = s->sr;
        break;
    case IMX_MU_FCR:
        value = s->fcr;
        break;
    case IMX_MU_FSR:
        value = s->fsr;
        break;
    case IMX_MU_GIER:
        value = s->gier;
        break;
    case IMX_MU_GCR:
        value = s->gcr;
        break;
    case IMX_MU_GSR:
        value = s->gsr;
        break;
    case IMX_MU_TCR:
        value = s->tcr;
        break;
    case IMX_MU_TSR:
        value = s->tsr;
        break;
    case IMX_MU_RCR:
        value = s->rcr;
        break;
    case IMX_MU_RSR:
        value = s->rsr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
    return value;
}

static void imx_mu_write(void *opaque, hwaddr offset,
                         uint64_t value, unsigned size)
{
    IMXMUState *s = opaque;

    if (offset >= IMX_MU_TR_BASE &&
        offset < IMX_MU_TR_BASE + IMX_MU_NUM_CHANNELS * 4) {
        unsigned idx = (offset - IMX_MU_TR_BASE) / 4;
        s->tr[idx] = value;
        trace_imx_mu_tr_write(idx, value);
        /*
         * Writing TR[n] clears TSR.TEn (TX empty). Three delivery modes:
         *  - TR-write handler (a responder, e.g. the ELE server): consumes
         *    synchronously and we re-set TEn so the next write succeeds
         *    without polling.
         *  - peer linked (the other side of a real MU, e.g. the A55<->M7
         *    rpmsg channel on MU7): deliver the word into the peer's RR[n]
         *    and raise its RX interrupt. TEn stays clear until the peer
         *    reads RR[n] (see the RR read path), which is the real MU TX
         *    full/empty handshake the imx-mailbox driver waits on.
         *  - neither (unattached MU): the slot stays "full" forever, which
         *    is correct for a mailbox with nothing on the far side.
         */
        s->tsr &= ~IMX_MU_V2_BIT(idx);
        if (s->tr_write_handler) {
            s->tr_write_handler(s->tr_write_opaque, idx, value);
            s->tsr |= IMX_MU_V2_BIT(idx);
        } else if (s->peer) {
            imx_mu_deliver_rr(s->peer, idx, (uint32_t)value);
        }
        imx_mu_update_irq(s);
        return;
    }
    if (offset >= IMX_MU_RR_BASE &&
        offset < IMX_MU_RR_BASE + IMX_MU_NUM_CHANNELS * 4) {
        /* RR is RO from the guest's perspective. */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only RR at 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    switch (offset) {
    case IMX_MU_VER:
    case IMX_MU_PAR:
        /* Read-only. Silent ignore matches HW. */
        break;

    case IMX_MU_CR:
        if (value & IMX_MU_CR_RST) {
            imx_mu_reset_state(s);
            imx_mu_update_irq(s);
            return;
        }
        s->cr = value;
        break;

    case IMX_MU_SR:
        /*
         * Per the kernel/U-Boot drivers, SR bits in V2 are read-only
         * status that the model manages. Writing has no effect.
         */
        break;

    case IMX_MU_FCR:
        /*
         * Flag-update register. On hardware a written flag bit is reflected
         * to the peer's FSR; the SCMI transport does not use these flags, so
         * we just store the value (no peer reflection or interrupt modelled).
         */
        s->fcr = value;
        break;

    case IMX_MU_FSR:
        /* Write-1-to-clear of latched flag-status bits. */
        s->fsr &= ~value;
        imx_mu_update_irq(s);
        break;

    case IMX_MU_GIER:
        s->gier = value;
        imx_mu_update_irq(s);
        break;

    case IMX_MU_GCR: {
        /*
         * GCR.GIRn writes are doorbell triggers. Detect 0->1 transitions
         * and, for each newly-asserted channel, deliver the doorbell. The
         * bit is auto-cleared afterwards (write-1-to-trigger pulse),
         * mirroring real HW where the request clears once the peer ACKs.
         *
         * Two delivery modes:
         *  - peer linked (the other side of a real MU): latch the matching
         *    GSR.GIPn on the peer and recompute the peer's IRQ. GIPn latches
         *    regardless of the peer's GIER, so a doorbell that arrives before
         *    the peer enables GIER.GIEn fires the moment it does (the peer's
         *    GIER write recomputes the IRQ) - real pending-vs-enable HW.
         *  - doorbell handler (a responder, e.g. the ELE server): invoke it
         *    synchronously to consume the request.
         */
        uint32_t mask  = (1u << IMX_MU_NUM_CHANNELS) - 1u;
        uint32_t newly = (value & ~s->gcr) & mask;
        s->gcr = value;
        for (unsigned i = 0; i < IMX_MU_NUM_CHANNELS; i++) {
            if (!(newly & IMX_MU_V2_BIT(i))) {
                continue;
            }
            trace_imx_mu_doorbell(i);
            if (s->peer) {
                s->peer->gsr |= IMX_MU_V2_BIT(i);
                imx_mu_update_irq(s->peer);
                s->gcr &= ~IMX_MU_V2_BIT(i);
            } else if (s->doorbell_handler) {
                s->doorbell_handler(s->doorbell_opaque, i);
                s->gcr &= ~IMX_MU_V2_BIT(i);
            }
        }
        imx_mu_update_irq(s);
        break;
    }

    case IMX_MU_GSR:
        /* Write-1-to-clear of GIP bits. */
        s->gsr &= ~value;
        imx_mu_update_irq(s);
        break;

    case IMX_MU_TCR:
        s->tcr = value;
        imx_mu_update_irq(s);
        break;

    case IMX_MU_TSR:
        /* W1C of TX status. */
        s->tsr &= ~value;
        imx_mu_update_irq(s);
        break;

    case IMX_MU_RCR:
        s->rcr = value;
        imx_mu_update_irq(s);
        break;

    case IMX_MU_RSR:
        /* W1C of RX status. */
        s->rsr &= ~value;
        imx_mu_update_irq(s);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx
                      " value 0x%" PRIx64 "\n",
                      __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps imx_mu_ops = {
    .read = imx_mu_read,
    .write = imx_mu_write,
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

/*
 * "peer" accepts any TYPE_IMX_MU and is settable at any time: the SM/M7 MUB
 * endpoint is only created (and hence linkable) once its M-core is realized,
 * so the SoC establishes the link after realize. A non-NULL check is what
 * makes the link writable; the type is already enforced by the link itself.
 */
static void imx_mu_peer_check(const Object *obj, const char *name,
                              Object *val, Error **errp)
{
}

static void imx_mu_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMXMUState *s = IMX_MU(obj);

    memory_region_init_io(&s->iomem, obj, &imx_mu_ops, s,
                          TYPE_IMX_MU, IMX_MU_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    /*
     * "peer" links the two endpoints (MUA/MUB) of one physical MU. It is a
     * weak link - both endpoints are owned by the SoC container, so neither
     * refs the other - and is settable after realize, because the SM/M7 MUB
     * side is only created once the M-core it faces exists. That is why this
     * uses object_property_add_link() rather than a static DEFINE_PROP_LINK
     * (which forbids setting a link once the device is realized).
     */
    object_property_add_link(obj, "peer", TYPE_IMX_MU,
                             (Object **)&s->peer, imx_mu_peer_check, 0);
}

static void imx_mu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_imx_mu;
    rc->phases.hold = imx_mu_reset_at_boot_hold;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "NXP i.MX Messaging Unit (V2)";
}

static const TypeInfo imx_mu_info = {
    .name           = TYPE_IMX_MU,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMXMUState),
    .instance_init  = imx_mu_init,
    .class_init     = imx_mu_class_init,
};

static void imx_mu_register_types(void)
{
    type_register_static(&imx_mu_info);
}

type_init(imx_mu_register_types)
