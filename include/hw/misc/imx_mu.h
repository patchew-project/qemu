/*
 * NXP i.MX Messaging Unit (MU) device model - V2 register layout
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Models the V2 MU register layout used by i.MX 95 (compatible
 * "fsl,imx95-mu") and other recent NXP SoCs. The MU is the
 * messaging channel between the Cortex-A55 cluster and the Cortex-M33
 * System Manager (and ELE, V2X, camera-mix, etc., depending on which
 * instance); it carries SCMI traffic for clock / pinctrl / power-
 * domain operations.
 */

#ifndef IMX_MU_H
#define IMX_MU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX_MU "imx.mu"
OBJECT_DECLARE_SIMPLE_TYPE(IMXMUState, IMX_MU)

#define IMX_MU_REG_SIZE         0x1000
#define IMX_MU_NUM_CHANNELS     4       /* TR[0..3] / RR[0..3] */

/*
 * V2 register offsets. Source: NXP U-Boot imx_mu_cfg_imx95 in
 * U-Boot drivers/mailbox/imx-mailbox.c.
 */
#define IMX_MU_VER              0x000   /* version (RO) */
#define IMX_MU_PAR              0x004   /* parameter (RO; TR/RR counts) */
#define IMX_MU_CR               0x008   /* control */
#define IMX_MU_SR               0x00C   /* status */
#define IMX_MU_FCR              0x100   /* flag control */
#define IMX_MU_FSR              0x104   /* flag status */
#define IMX_MU_GIER             0x110   /* general-purpose IRQ enable */
#define IMX_MU_GCR              0x114   /* general-purpose control */
#define IMX_MU_GSR              0x118   /* general-purpose status (GIP bits) */
#define IMX_MU_TCR              0x120   /* TX IRQ enable */
#define IMX_MU_TSR              0x124   /* TX status (TEn = TX empty) */
#define IMX_MU_RCR              0x128   /* RX IRQ enable */
#define IMX_MU_RSR              0x12C   /* RX status (RFn = RX full) */
#define IMX_MU_TR_BASE          0x200   /* TR[0..15] (TX data) */
#define IMX_MU_RR_BASE          0x280   /* RR[0..15] (RX data) */

/* Helpers for V2 bit numbering (one bit per channel index, starting at 0). */
#define IMX_MU_V2_BIT(idx)      (1u << (idx))

/* CR.RST clears all state when written (V2 bit 0). */
#define IMX_MU_CR_RST           BIT(0)

/*
 * Optional callback invoked when the guest writes a 0->1 transition
 * on any GCR.GIRn bit (a doorbell trigger from the agent side). A
 * responder (e.g. the ELE server) registers itself here to process
 * inbound messages. Unset by default; the model functions as a plain
 * register file when no handler is registered.
 */
typedef void (*IMXMUDoorbellHandler)(void *opaque, unsigned int idx);

/*
 * Optional callback invoked when the guest writes to a TR[idx] register.
 * The ELE responder stub uses this to accumulate incoming ELE-protocol
 * message words and react when a full message has arrived. Unset by
 * default; the model is a plain register file with no consumer.
 */
typedef void (*IMXMUTRWriteHandler)(void *opaque, unsigned int idx,
                                    uint32_t value);

struct IMXMUState {
    SysBusDevice    parent_obj;

    MemoryRegion    iomem;
    qemu_irq        irq;

    /*
     * Writable register state. Read-only registers (VER, PAR) are
     * computed at read time from the configured channel count.
     */
    uint32_t        cr;
    uint32_t        sr;
    uint32_t        fcr;
    uint32_t        fsr;
    uint32_t        gier;
    uint32_t        gcr;
    uint32_t        gsr;
    uint32_t        tcr;
    uint32_t        tsr;
    uint32_t        rcr;
    uint32_t        rsr;
    uint32_t        tr[IMX_MU_NUM_CHANNELS];
    uint32_t        rr[IMX_MU_NUM_CHANNELS];

    /* Doorbell forwarding (see typedef above). */
    IMXMUDoorbellHandler doorbell_handler;
    void                *doorbell_opaque;

    /* TR-write forwarding (see typedef above). */
    IMXMUTRWriteHandler  tr_write_handler;
    void                *tr_write_opaque;

    /*
     * Optional peer MU endpoint (the other side of a real hardware MU).
     * When set, a GCR.GIRn doorbell trigger on this side latches the
     * matching GSR.GIPn on the peer (raising the peer's IRQ once the peer
     * enables GIER.GIEn), instead of invoking doorbell_handler. This models
     * the A55-side (MUA) <-> M33-side (MUB) cross-connect used to let the
     * real SM firmware service the A55's SCMI traffic. Set via the "peer"
     * QOM link property (registered in imx_mu_init()).
     */
    IMXMUState          *peer;
};

/*
 * Register a doorbell handler. Replaces any previously registered
 * handler. Call with handler = NULL to deregister.
 */
void imx_mu_set_doorbell_handler(IMXMUState *s,
                                 IMXMUDoorbellHandler handler,
                                 void *opaque);

/* Register a TR-write handler. NULL to deregister. */
void imx_mu_set_tr_write_handler(IMXMUState *s,
                                 IMXMUTRWriteHandler handler,
                                 void *opaque);

/*
 * Deliver a response word into RR[idx] and assert RSR.RFn so the
 * agent's mu_hal_receivemsg() poll loop exits. Used by responder
 * stubs (e.g. ELE) to push a message back to the agent.
 */
void imx_mu_deliver_rr(IMXMUState *s, unsigned int idx, uint32_t value);

/*
 * External hook: peripheral code (e.g. a responder) can call
 * this to assert a GP-interrupt-pending bit on a particular channel,
 * which surfaces as GSR.GIPn and (if GIER.GIEn is set) raises the IRQ.
 * Used by the response side of a doorbell handshake.
 */
void imx_mu_assert_gip(IMXMUState *s, unsigned int idx);

#endif /* IMX_MU_H */
