/*
 * Microchip PIC32MK CAN FD controller emulation
 * Based on PIC32MK GPK/MCM with CAN FD Family Datasheet (DS60001519E)
 * and Microchip CAN FD Controller Reference Manual (DS60001507).
 *
 * Implements:
 *  - Two MemoryRegions per instance: SFR block + Message RAM
 *  - Operating mode transitions (Config / Normal / Internal Loopback / others)
 *  - TX Queue (TXQ) and up to 31 configurable FIFOs
 *  - UINC pointer-advance protocol (head/tail management)
 *  - Internal loopback: TX → acceptance filter → RX FIFO
 *  - CiINT two-level interrupt aggregator → single EVIC IRQ line
 *  - 32 acceptance filters with mask
 *  - SET/CLR/INV register aliasing for key registers
 *
 * Not yet implemented:
 *  - CiFIFOBA register (message RAM base set by firmware; needed for
 *    Harmony3-generated drivers that call CFD1FIFOBA = KVA_TO_PA(buf))
 *  - TEF (TX Event FIFO) population on TX complete
 *  - CiTBC free-running counter (timestamp)
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/mips/pic32mk.h"
#include "hw/mips/pic32mk_canfd.h"

/*
 * DLC / PLSIZE helpers
 * -----------------------------------------------------------------------
 */

static const uint8_t dlc_to_bytes_fd[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
};

static uint8_t canfd_dlc_bytes(uint8_t dlc, bool fdf)
{
    if (dlc > 8 && !fdf) {
        return 8;
    }
    return dlc_to_bytes_fd[dlc & 0xF];
}

/* PLSIZE field → payload bytes */
static const uint8_t plsize_to_bytes[8] = { 8, 12, 16, 20, 24, 32, 48, 64 };

static uint32_t canfd_obj_size(uint32_t fifocon)
{
    uint8_t plsize = (fifocon >> CANFD_FIFO_PLSIZE_SHIFT) & 0x7u;
    return 8u + plsize_to_bytes[plsize];   /* 2 header words + payload */
}

/*
 * Message RAM layout helpers
 *
 * Layout (sequential from msg_ram_phys):
 *   [TXQ  region] if TXQEN
 *   [TEF  region] if STEF
 *   [FIFO 1..31]
 *
 * For simplicity we pre-allocate the maximum per region and compute
 * offsets statically from the FIFO configuration registers.
 * -----------------------------------------------------------------------
 */

#define CANFD_MAX_OBJ_SIZE  72u   /* 8 header + 64 payload */
#define CANFD_MAX_DEPTH     32u   /* FSIZE+1 max */

static uint32_t canfd_txq_ram_base(PIC32MKCANFDState *s)
{
    return s->msg_ram_phys;
}

static uint32_t canfd_txq_obj_size(PIC32MKCANFDState *s)
{
    return canfd_obj_size(s->txqcon);
}

static uint32_t canfd_txq_depth(PIC32MKCANFDState *s)
{
    return ((s->txqcon >> CANFD_FIFO_FSIZE_SHIFT) & 0x1Fu) + 1u;
}

/* Region base for FIFO n (1-based) within the message RAM buffer */
static uint32_t canfd_fifo_ram_base(PIC32MKCANFDState *s, int n)
{
    /* TXQ region */
    uint32_t off = 0;
    if (s->con & CANFD_CON_TXQEN) {
        off += canfd_txq_obj_size(s) * canfd_txq_depth(s);
    }
    /* TEF region (each TEF object = 8 bytes, no payload) */
    if (s->con & CANFD_CON_STEF) {
        uint32_t tef_depth = ((s->tefcon >> CANFD_FIFO_FSIZE_SHIFT) & 0x1Fu) + 1u;
        off += 8u * tef_depth;
    }
    /* FIFOs 1..n-1 */
    for (int i = 1; i < n; i++) {
        uint32_t depth = ((s->fifocon[i] >> CANFD_FIFO_FSIZE_SHIFT) & 0x1Fu) + 1u;
        off += canfd_obj_size(s->fifocon[i]) * depth;
    }
    return s->msg_ram_phys + off;
}

static uint32_t canfd_fifo_slot_ua(PIC32MKCANFDState *s, int n, uint8_t slot)
{
    return canfd_fifo_ram_base(s, n) + (uint32_t)slot * canfd_obj_size(s->fifocon[n]);
}

static uint32_t canfd_txq_slot_ua(PIC32MKCANFDState *s, uint8_t slot)
{
    return canfd_txq_ram_base(s) + (uint32_t)slot * canfd_txq_obj_size(s);
}

/*
 * IRQ update — recomputes CiINT status bits and asserts/deasserts IRQ
 * -----------------------------------------------------------------------
 */

static void canfd_update_irq(PIC32MKCANFDState *s)
{
    bool fire = false;

    /* RXIF: any RX FIFO has data */
    if (s->cint & CANFD_INT_RXIE) {
        if (s->rxif) {
            s->cint |= CANFD_INT_RXIF;
            fire = true;
        } else {
            s->cint &= ~CANFD_INT_RXIF;
        }
    }

    /* TXIF: any TX FIFO / TXQ completed */
    if (s->cint & CANFD_INT_TXIE) {
        if (s->txif) {
            s->cint |= CANFD_INT_TXIF;
            fire = true;
        } else {
            s->cint &= ~CANFD_INT_TXIF;
        }
    }

    /* MODIF: mode changed and MODIE enabled */
    if ((s->cint & CANFD_INT_MODIE) && (s->cint & CANFD_INT_MODIF)) {
        fire = true;
    }

    /* RXOVIF: any RX overflow and RXOVIE enabled */
    if ((s->cint & CANFD_INT_RXOVIE) && (s->cint & CANFD_INT_RXOVIF)) {
        fire = true;
    }

    qemu_set_irq(s->irq, fire ? 1 : 0);
}

/*
 * Acceptance filter matching
 * Returns destination FIFO index (1-based) or -1 if no match.
 * -----------------------------------------------------------------------
 */

static int canfd_find_fifo(PIC32MKCANFDState *s, uint32_t id, bool xtd)
{
    for (int n = 0; n < 32; n++) {
        /* Each CiFLTCON register holds 4 filter bytes */
        uint8_t fltcon_byte = (s->fltcon[n / 4] >> ((n % 4) * 8)) & 0xFFu;
        if (!(fltcon_byte & 0x80u)) {
            continue;   /* FLTEN = 0 */
        }

        uint32_t obj  = s->fltobj[n];
        uint32_t msk  = s->mask[n];

        /*
         * MIDE bit (bit 29 of CiMASKn): when set, the filter only matches
         * frames whose IDE bit equals FLTOBJ.IDE (bit 30).
         * When MIDE=0, the filter accepts both standard and extended frames.
         */
        if (msk & (1u << 29u)) {
            bool obj_xtd = (obj >> 30) & 1u;
            if (obj_xtd != xtd) {
                continue;
            }
        }

        /* (frame_id XOR filter_id) AND mask == 0 means match */
        if ((id ^ (obj & 0x1FFFFFFFu)) & (msk & 0x1FFFFFFFu)) {
            continue;
        }

        return fltcon_byte & 0x1Fu;   /* destination FIFO */
    }
    return -1;
}

/*
 * RX deliver — write an incoming frame into the matching RX FIFO
 * -----------------------------------------------------------------------
 */

static void canfd_rx_deliver(PIC32MKCANFDState *s,
                             uint32_t id, bool xtd, bool fdf,
                             uint8_t dlc, const uint8_t *data, int len,
                             int filter_hit)
{
    int dest = filter_hit;
    if (dest < 1 || dest > 31) {
        return;
    }

    uint32_t depth = ((s->fifocon[dest] >> CANFD_FIFO_FSIZE_SHIFT) & 0x1Fu) + 1u;
    if (s->fifo_count[dest] >= (uint8_t)depth) {
        /* Overflow */
        s->rxovif |= (1u << dest);
        s->cint   |= CANFD_INT_RXOVIF;
        canfd_update_irq(s);
        return;
    }

    /* Write message object to tail slot */
    uint32_t ua = canfd_fifo_slot_ua(s, dest, s->fifo_tail[dest]);
    uint32_t ram_off = ua - s->msg_ram_phys;
    uint8_t *obj = s->msg_ram_buf + ram_off;
    uint32_t payload_cap = canfd_obj_size(s->fifocon[dest]) - 8u;
    uint8_t dlc_len = canfd_dlc_bytes(dlc, fdf);
    uint8_t copy_len = (uint8_t)MIN((uint32_t)MAX(len, 0), payload_cap);
    if (copy_len > dlc_len) {
        copy_len = dlc_len;
    }

    /*
     * RX message object R0/R1 layout (DS60001507 Table 38-2):
     *   R0[10:0]  : SID[10:0]  (or upper 11 of 29-bit EID)
     *   R0[28:11] : EID[17:0]  (lower 18 bits of 29-bit extended ID)
     *   R0[30]    : EXIDE      (1 = extended frame) — matches CiFLTOBJ layout
     *   R1[4]     : IDE        (1 = extended frame) — this is what plib_canfd checks
     * Note: TX T0 has the same SID/EID layout but IDE is in T1[4], not T0[30].
     */
    uint32_t r0;
    if (xtd) {
        /* 29-bit extended: SID = ID[28:18], EID = ID[17:0] */
        r0 = ((id >> 18) & 0x7FFu)          /* SID → r0 bits [10:0]  */
           | ((id & 0x3FFFFu) << 11)         /* EID → r0 bits [28:11] */
           | (1u << 30u);                    /* IDE = 1                */
    } else {
        r0 = id & 0x7FFu;                   /* SID → r0 bits [10:0]  */
    }
    *(uint32_t *)(obj + 0) = r0;

    /* R1: DLC, flags, FILHIT, RXTS=0 */
    uint32_t r1 = (uint32_t)dlc
                | (xtd ? (1u << 4) : 0u)
                | (fdf ? (1u << 7) : 0u)
                | ((uint32_t)(filter_hit & 0x1F) << 11);
    *(uint32_t *)(obj + 4) = r1;

    /*
     * RX message data area layout (DS60001507 Figure 3-2):
     *   data[0..3] : RXMSGTS — 32-bit receive timestamp (always present when
     *                RXTSEN=1 in FIFOCONn; firmware expects this field)
     *   data[4..]  : Payload bytes
     *
     * Harmony3 plib_canfd always reads payload from data[4] onwards, so we
     * must write a timestamp (even if 0) before the payload.
     */
    memset(obj + 8, 0, payload_cap);           /* Clear timestamp + payload */
    *(uint32_t *)(obj + 8) = s->tbc;           /* Write timestamp at data[0..3] */
    if (copy_len > 0 && data) {
        memcpy(obj + 12, data, copy_len);      /* Payload starts at data[4] */
    }

    /* Advance tail */
    s->fifo_tail[dest] = (s->fifo_tail[dest] + 1u) % (uint8_t)depth;
    s->fifo_count[dest]++;

    /*
     * Set interrupt flags — TFNRFNIF signals data present.
     * Only assert RXIF if TFNRFNIE (per-FIFO RX interrupt enable) is set;
     * otherwise the frame waits silently until firmware arms reception
     * via CAN_MessageReceive() which re-enables TFNRFNIE.
      */
    s->fifosta[dest] |= CANFD_FIFOSTA_TFNRFNIF;
    if (s->fifocon[dest] & CANFD_FIFO_TFNRFNIE) {
        s->rxif |= (1u << dest);
    }

    /*
     * Update UA: RX has no write slot; keep head slot for firmware read.
     */
    s->fifoua[dest] = canfd_fifo_slot_ua(s, dest, s->fifo_head[dest]);

    /* CiVEC.ICODE — firmware RX ISR reads this to learn which FIFO fired */
    s->vec = (uint32_t)dest & 0x7Fu;

    canfd_update_irq(s);
}

/*
 * TX processing — triggered when TXREQ is set for a FIFO or TXQ
 * -----------------------------------------------------------------------
 */

static void canfd_process_tx(PIC32MKCANFDState *s, int fifo)
{
    uint32_t ua;
    if (fifo == 0) {
        /* TXQ: transmit from head (oldest queued frame) */
        ua = canfd_txq_slot_ua(s, s->txq_head);
    } else {
        /* TX FIFO: transmit from head (oldest pending frame) */
        ua = canfd_fifo_slot_ua(s, fifo, s->fifo_head[fifo]);
    }

    uint32_t ram_off = ua - s->msg_ram_phys;
    if (ram_off >= PIC32MK_CAN_MSGRAM_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk-canfd: TX UA 0x%08x out of message RAM\n", ua);
        return;
    }

    const uint8_t *obj = s->msg_ram_buf + ram_off;
    uint32_t t0  = *(const uint32_t *)(obj + 0);
    uint32_t t1  = *(const uint32_t *)(obj + 4);
    uint8_t  dlc = t1 & 0xFu;
    bool     fdf = (t1 >> 7) & 1u;
    bool     xtd = (t1 >> 4) & 1u;   /* IDE in T1[4], not T0[30] per DS60001507 s3.1 */
    /* T0 ID layout: SID in bits[10:0], EID in bits[28:11] (same as R0/CiFLTOBJ) */
    uint32_t id  = xtd ? (((t0 & 0x7FFu) << 18) | ((t0 >> 11) & 0x3FFFFu))
                       : (t0 & 0x7FFu);
    int      len = canfd_dlc_bytes(dlc, fdf);
    const uint8_t *data = obj + 8;

    uint8_t opmod = (s->con >> CANFD_CON_OPMOD_SHIFT) & 0x7u;
    if (opmod == CANFD_OPMOD_INT_LOOP) {
        /* Internal loopback: deliver frame through acceptance filters */
        int dest = canfd_find_fifo(s, id, xtd);
        if (dest >= 1) {
            canfd_rx_deliver(s, id, xtd, fdf, dlc, data, len, dest);
        }
    } else {
        /* External TX: send frame to the virtual CAN bus */
        qemu_can_frame frame = {};
        frame.can_id  = xtd ? (id | QEMU_CAN_EFF_FLAG) : id;
        frame.can_dlc = (uint8_t)len;
        frame.flags   = fdf ? QEMU_CAN_FRMF_TYPE_FD : 0u;
        memcpy(frame.data, data, (size_t)len);
        if (s->canbus) {
            can_bus_client_send(&s->bus_client, &frame, 1);
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "pic32mk-canfd[%u]: no canbus connected, "
                          "dropping TX frame id=0x%08x\n", s->instance_id, id);
        }
    }

    /* TX complete bookkeeping */
    if (fifo == 0) {
        /* TXQ: advance head (oldest pending frame consumed) */
        uint8_t depth = (uint8_t)canfd_txq_depth(s);
        s->txq_head = (s->txq_head + 1u) % depth;
        if (s->txq_count > 0) {
            s->txq_count--;
        }
        s->txqcon  &= ~CANFD_FIFO_TXREQ;
        s->txqsta  |= CANFD_TXQSTA_TXQNIF;  /* slot available */
        s->txif    |= 1u;          /* bit 0 = TXQ */
        s->vec      = 0u;          /* ICODE=0 for TXQ */
    } else {
        uint32_t depth = ((s->fifocon[fifo] >> CANFD_FIFO_FSIZE_SHIFT) & 0x1Fu) + 1u;
        s->fifo_head[fifo] = (s->fifo_head[fifo] + 1u) % (uint8_t)depth;
        if (s->fifo_count[fifo] > 0) {
            s->fifo_count[fifo]--;
        }
        s->fifocon[fifo] &= ~CANFD_FIFO_TXREQ;
        s->fifosta[fifo] |= CANFD_FIFOSTA_TXATIF;
        /* TX slot freed — TX FIFO is no longer full */
        s->fifosta[fifo] |= CANFD_FIFOSTA_TFNRFNIF;
        s->txif |= (1u << fifo);
        s->vec   = (uint32_t)fifo & 0x7Fu;  /* ICODE = FIFO number */
    }

    canfd_update_irq(s);
}

/* Forward declaration — defined below canfd_receive() */
static void canfd_bus_buf_drain(PIC32MKCANFDState *s);

/*
 * UINC — advance head (RX) or tail (TX) pointer, clear UINC bit
 * -----------------------------------------------------------------------
 */

static void canfd_uinc_fifo(PIC32MKCANFDState *s, int n)
{
    bool is_tx = (s->fifocon[n] & CANFD_FIFO_TXEN) != 0u;
    uint32_t depth = ((s->fifocon[n] >> CANFD_FIFO_FSIZE_SHIFT) & 0x1Fu) + 1u;

    if (is_tx) {
        /* Firmware finished writing a TX object — advance tail */
        s->fifo_tail[n] = (s->fifo_tail[n] + 1u) % (uint8_t)depth;
        s->fifo_count[n]++;
        s->fifoua[n] = canfd_fifo_slot_ua(s, n, s->fifo_tail[n]);
        /* TX FIFO full — clear "not full" flag so firmware won't overwrite */
        if (s->fifo_count[n] >= (uint8_t)depth) {
            s->fifosta[n] &= ~CANFD_FIFOSTA_TFNRFNIF;
        }
    } else {
        /* Firmware finished reading an RX object — advance head */
        if (s->fifo_count[n] > 0) {
            s->fifo_count[n]--;
        }
        s->fifo_head[n] = (s->fifo_head[n] + 1u) % (uint8_t)depth;
        s->fifoua[n] = canfd_fifo_slot_ua(s, n, s->fifo_head[n]);

        if (s->fifo_count[n] == 0) {
            s->rxif     &= ~(1u << n);
            s->fifosta[n] &= ~CANFD_FIFOSTA_TFNRFNIF;
        }
        canfd_update_irq(s);
        /*
         * Drain bus_buf after UINC: firmware just freed a FIFO slot, so the
         * next buffered frame can be delivered immediately.  In polling mode
         * the firmware checks FIFOSTA.TFNRFNIF directly after UINC; delivering
         * here ensures that flag is set before the next poll.  In interrupt
         * mode with TFNRFNIE enabled, canfd_rx_deliver will re-raise the IRQ
         * for the newly delivered frame.
          */
        canfd_bus_buf_drain(s);
    }
}

static void canfd_uinc_txq(PIC32MKCANFDState *s)
{
    uint8_t depth = (uint8_t)canfd_txq_depth(s);
    /* Advance tail — firmware finished writing a TX object */
    s->txq_tail = (s->txq_tail + 1u) % depth;
    s->txq_count++;
    /* UA now points at the next empty write slot */
    s->txqua = canfd_txq_slot_ua(s, s->txq_tail);
}

/*
 * FRESET — reset a FIFO to empty state
 * -----------------------------------------------------------------------
 */

static void canfd_freset_fifo(PIC32MKCANFDState *s, int n)
{
    s->fifo_head[n]  = 0;
    s->fifo_tail[n]  = 0;
    s->fifo_count[n] = 0;
    s->rxif         &= ~(1u << n);
    s->txif         &= ~(1u << n);
    s->fifosta[n]    = 0;
    /* TX FIFO: empty after reset means "not full" → TFNRFNIF = 1 */
    if (s->fifocon[n] & CANFD_FIFO_TXEN) {
        s->fifosta[n] |= CANFD_FIFOSTA_TFNRFNIF;
    }
    s->fifoua[n]     = canfd_fifo_slot_ua(s, n, 0);
    canfd_update_irq(s);
}

/*
 * Abort all pending TX (CiCON.ABAT)
 * -----------------------------------------------------------------------
 */

static void canfd_abort_all_tx(PIC32MKCANFDState *s)
{
    /* TXQ */
    s->txqcon  &= ~CANFD_FIFO_TXREQ;
    s->txqsta  |= CANFD_TXQSTA_TXQNIF;

    /* TX FIFOs */
    for (int i = 1; i < 32; i++) {
        if (s->fifocon[i] & CANFD_FIFO_TXEN) {
            s->fifocon[i] &= ~CANFD_FIFO_TXREQ;
            s->fifosta[i] |= CANFD_FIFOSTA_TXATIF;
            s->txif |= (1u << i);
        }
    }
    canfd_update_irq(s);
}

/*
 * SocketCAN virtual bus callbacks
 *
 * When a can-bus object is linked (via the "canbus" QOM property), the
 * device behaves as a bus participant:
 *   TX (non-loopback): canfd_process_tx() calls can_bus_client_send()
 *   RX (from bus):     canfd_receive() delivers frames through the
 *                      acceptance filter into an RX FIFO
 * -----------------------------------------------------------------------
 */

static bool canfd_can_receive(CanBusClientState *client)
{
    PIC32MKCANFDState *s = container_of(client, PIC32MKCANFDState, bus_client);
    uint8_t opmod = (s->con >> CANFD_CON_OPMOD_SHIFT) & 0x7u;
    /*
     * Accept incoming frames in Normal, External Loopback, Listen-only,
     * and Restricted modes — not in Config or Internal Loopback mode.
      */
    return opmod == CANFD_OPMOD_NORMAL
        || opmod == CANFD_OPMOD_EXT_LOOP
        || opmod == CANFD_OPMOD_LISTEN
        || opmod == CANFD_OPMOD_RESTRICTED;
}

/*
 * Drain the software bus buffer into hardware RX FIFOs.
 * Called after each RX UINC — the firmware just freed a FIFO slot so we can
 * deliver at most one buffered frame per UINC (matching real FIFO depth-1
 * behaviour).  Stops when the head frame's target FIFO is still full.
 */
static void canfd_bus_buf_drain(PIC32MKCANFDState *s)
{
    while (s->bus_buf_count > 0) {
        int n = s->bus_buf_head;
        int dest = s->bus_buf_dest[n];
        uint32_t depth = ((s->fifocon[dest] >> CANFD_FIFO_FSIZE_SHIFT) & 0x1Fu) + 1u;
        if (s->fifo_count[dest] >= (uint8_t)depth) {
            break;  /* target FIFO still full — wait for next UINC */
        }
        canfd_rx_deliver(s,
                         s->bus_buf_id[n], s->bus_buf_xtd[n],
                         s->bus_buf_fdf[n], s->bus_buf_dlc[n],
                         s->bus_buf_data[n], s->bus_buf_len[n], dest);
        s->bus_buf_head = (s->bus_buf_head + 1) % 64;
        s->bus_buf_count--;
    }
}

static ssize_t canfd_receive(CanBusClientState *client,
                             const qemu_can_frame *frames, size_t frames_cnt)
{
    PIC32MKCANFDState *s = container_of(client, PIC32MKCANFDState, bus_client);

    for (size_t i = 0; i < frames_cnt; i++) {
        const qemu_can_frame *f = &frames[i];
        bool     xtd = (f->can_id & QEMU_CAN_EFF_FLAG) != 0;
        uint32_t id  = xtd ? (f->can_id & QEMU_CAN_EFF_MASK)
                           : (f->can_id & QEMU_CAN_SFF_MASK);
        bool     fdf = (f->flags & QEMU_CAN_FRMF_TYPE_FD) != 0;
        uint8_t  dlc = can_len2dlc(f->can_dlc);
        uint8_t  rx_len = MIN((uint8_t)f->can_dlc, canfd_dlc_bytes(dlc, fdf));
        int      dest = canfd_find_fifo(s, id, xtd);

        /* Log received frame with full payload hex dump */

        if (dest < 1) {
            continue;  /* no filter match */
        }

        uint32_t depth = ((s->fifocon[dest] >> CANFD_FIFO_FSIZE_SHIFT) & 0x1Fu) + 1u;
        if (s->fifo_count[dest] < (uint8_t)depth) {
            /* FIFO has space — deliver directly */
            canfd_rx_deliver(s, id, xtd, fdf, dlc, f->data, rx_len, dest);
        } else if (s->bus_buf_count < 64) {
            /* FIFO full — buffer for later delivery after UINC */
            int slot = s->bus_buf_tail;
            s->bus_buf_id[slot]  = id;
            s->bus_buf_xtd[slot] = xtd;
            s->bus_buf_fdf[slot] = fdf;
            s->bus_buf_dlc[slot] = dlc;
            s->bus_buf_len[slot] = rx_len;
            s->bus_buf_dest[slot] = dest;
            memcpy(s->bus_buf_data[slot], f->data, rx_len);
            s->bus_buf_tail = (s->bus_buf_tail + 1) % 64;
            s->bus_buf_count++;
        } else {
            /* Software buffer also full — real bus overflow */
            s->rxovif |= (1u << dest);
            s->cint   |= CANFD_INT_RXOVIF;
            canfd_update_irq(s);
        }
    }
    return (ssize_t)frames_cnt;
}

static CanBusClientInfo canfd_bus_client_info = {
    .can_receive = canfd_can_receive,
    .receive     = canfd_receive,
};

/*
 * SFR MMIO read
 *
 * Each logical register occupies 0x10 bytes: +0=base, +4=SET, +8=CLR, +C=INV.
 * Reads always return the base register value regardless of sub-offset.
 * -----------------------------------------------------------------------
 */

static uint64_t canfd_sfr_read(void *opaque, hwaddr offset, unsigned size)
{
    PIC32MKCANFDState *s = opaque;
    /* Strip SET/CLR/INV sub-offset: base is at (offset & ~0xCu) */
    hwaddr base_off = offset & ~0xCu;

    switch (base_off) {
    case CANFD_CiCON:
        return s->con;
    case CANFD_CiNBTCFG:
        return s->nbtcfg;
    case CANFD_CiDBTCFG:
        return s->dbtcfg;
    case CANFD_CiTDC:
        return s->tdc;
    case CANFD_CiTBC:
        return s->tbc;
    case CANFD_CiTSCON:
        return s->tscon;
    case CANFD_CiVEC:
        return s->vec;
    case CANFD_CiINT:
        return s->cint;
    case CANFD_CiRXIF:
        return s->rxif;
    case CANFD_CiTXIF:
        return s->txif;
    case CANFD_CiRXOVIF:
        return s->rxovif;
    case CANFD_CiTXATIF:
        return 0;
    case CANFD_CiTXREQ:
        return s->txreq;
    case CANFD_CiTREC:
        return s->trec;
    case CANFD_CiTEFCON:
        return s->tefcon;
    case CANFD_CiTEFSTA:
        return s->tefsta;
    case CANFD_CiTEFUA:
        return s->tefua;
    case CANFD_CiFIFOBA:
        return 0;
    case CANFD_CiTXQCON:
        return s->txqcon;
    case CANFD_CiTXQSTA:
        return s->txqsta;
    case CANFD_CiTXQUA:
        return s->txqua;
    default:
        break;
    }

    /*
     * FIFO registers: base 0x170, stride 0x30 per FIFO (n=1..31)
     *   +0x00: CiFIFOCONn, +0x10: CiFIFOSTAn, +0x20: CiFIFOUAn
      */
    if (base_off >= CANFD_CiFIFOCON(1) && base_off <= CANFD_CiFIFOCON(31) + 0x20u) {
        int idx = (int)((base_off - 0x170u) / 0x30u) + 1;
        int sub = (int)((base_off - 0x170u) % 0x30u);
        if (idx >= 1 && idx <= 31) {
            if (sub == 0x00) {
                return s->fifocon[idx];
            }
            if (sub == 0x10) {
                return s->fifosta[idx];
            }
            if (sub == 0x20) {
                return s->fifoua[idx];
            }
        }
    }

    /* Filter control: stride 0x10 per register (r=0..7) */
    if (base_off >= CANFD_CiFLTCON(0) && base_off <= CANFD_CiFLTCON(7)) {
        return s->fltcon[(base_off - 0x740u) / 0x10u];
    }

    /* Filter obj/mask: stride 0x20 per pair (n=0..31) */
    if (base_off >= CANFD_CiFLTOBJ(0) && base_off <= CANFD_CiMASK(31)) {
        if (base_off >= 0x7C0u && base_off < 0x7C0u + 32u * 0x20u) {
            int fn = (int)((base_off - 0x7C0u) / 0x20u);
            return s->fltobj[fn];
        }
        if (base_off >= 0x7D0u && base_off < 0x7D0u + 32u * 0x20u) {
            int fn = (int)((base_off - 0x7D0u) / 0x20u);
            return s->mask[fn];
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk-canfd: unimplemented SFR read @ +0x%03" HWADDR_PRIx "\n",
                  offset);
    return 0;
}

/*
 * Apply a CLR/SET/INV write to a register value.
 * PIC32MK convention: sub=0 → plain write, +4 → CLR, +8 → SET, +0xC → INV
 * -----------------------------------------------------------------------
 */
static uint32_t apply_sci(uint32_t cur, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        return val;
    case 4:
        return cur & ~val;
    case 8:
        return cur | val;
    case 0xC:
        return cur ^ val;
    default:
        return cur;
    }
}

/*
 * SFR MMIO write
 *
 * Each logical register occupies 0x10 bytes: +0=base, +4=SET, +8=CLR, +C=INV.
 * UINC, TXREQ, FRESET are pulse bits — handled then stripped from storage.
 * -----------------------------------------------------------------------
 */

static void canfd_sfr_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    PIC32MKCANFDState *s = opaque;
    uint32_t v32     = (uint32_t)val;
    hwaddr   base_off = offset & ~0xCu;
    int      sub     = (int)(offset & 0xCu);
    uint8_t  opmod   = (s->con >> CANFD_CON_OPMOD_SHIFT) & 0x7u;

    switch (base_off) {

    /* ---- CiCON ---- */
    case CANFD_CiCON: {
        uint8_t old_opmod = opmod;
        uint32_t new_con  = apply_sci(s->con, v32, sub);

        uint8_t reqop = (new_con >> CANFD_CON_REQOP_SHIFT) & 0x7u;
        /* Reflect REQOP into OPMOD immediately; BUSY always 0 */
        new_con = (new_con & ~CANFD_CON_OPMOD_MASK)
                | ((uint32_t)reqop << CANFD_CON_OPMOD_SHIFT);
        s->con = new_con;

        if (reqop != old_opmod) {
            s->cint |= CANFD_INT_MODIF;
            canfd_update_irq(s);
        }

        /* Handle ABAT */
        if (new_con & CANFD_CON_ABAT) {
            canfd_abort_all_tx(s);
            s->con &= ~CANFD_CON_ABAT;
        }

        /* Initialise TXQ UA when TXQEN becomes set */
        if ((new_con & CANFD_CON_TXQEN) && !(s->txqua)) {
            s->txqua = canfd_txq_slot_ua(s, s->txq_tail);
        }
        return;
    }

    /* ---- Bit-time / TDC — only writable in Config mode ---- */
    case CANFD_CiNBTCFG:
        if (opmod == CANFD_OPMOD_CONFIG) {
            s->nbtcfg = apply_sci(s->nbtcfg, v32, sub);
        }
        return;
    case CANFD_CiDBTCFG:
        if (opmod == CANFD_OPMOD_CONFIG) {
            s->dbtcfg = apply_sci(s->dbtcfg, v32, sub);
        }
        return;
    case CANFD_CiTDC:
        if (opmod == CANFD_OPMOD_CONFIG) {
            s->tdc = apply_sci(s->tdc, v32, sub);
        }
        return;
    case CANFD_CiTSCON:
        s->tscon = apply_sci(s->tscon, v32, sub);
        return;

    /* CiINT: enable bits [31:16] writable; status [15:0] cleared by fw */
    case CANFD_CiINT:
        s->cint = apply_sci(s->cint, v32, sub);
        canfd_update_irq(s);
        return;

    /* ---- CiRXIF / CiTXIF / CiRXOVIF — firmware clears by writing 0 ---- */
    case CANFD_CiRXIF:
        s->rxif = apply_sci(s->rxif, v32, sub);
        canfd_update_irq(s);
        return;
    case CANFD_CiTXIF:
        s->txif = apply_sci(s->txif, v32, sub);
        canfd_update_irq(s);
        return;
    case CANFD_CiRXOVIF:
        s->rxovif = apply_sci(s->rxovif, v32, sub);
        return;

    /* ---- CiTXREQ — writing 1 to a bit requests TX for that FIFO ---- */
    case CANFD_CiTXREQ: {
        uint32_t new_req = apply_sci(s->txreq, v32, sub);
        uint32_t newly   = new_req & ~s->txreq;
        s->txreq = new_req;
        for (int i = 1; i < 32; i++) {
            if (newly & (1u << i)) {
                canfd_process_tx(s, i);
            }
        }
        return;
    }

    /* ---- CiTXQCON ---- */
    case CANFD_CiTXQCON: {
        uint32_t new_con = apply_sci(s->txqcon, v32, sub);
        bool uinc = (new_con & CANFD_FIFO_UINC)  != 0u;
        bool req  = (new_con & CANFD_FIFO_TXREQ) != 0u;
        s->txqcon = new_con & ~(CANFD_FIFO_UINC | CANFD_FIFO_TXREQ
                                | CANFD_FIFO_FRESET);
        /*
         * Clearing TXQEIE (bit 4) acknowledges TX completion — deassert TXQ
         * source in txif so TXIF is not re-asserted on the next irq update.
          */
        if (!(s->txqcon & (1u << 4u))) {
            s->txif &= ~1u;
            canfd_update_irq(s);
        }
        if (uinc) {
            canfd_uinc_txq(s);
        }
        if (req) {
            canfd_process_tx(s, 0);
        }
        return;
    }

    /* ---- CiTXQSTA / CiTXQUA — hardware-managed, ignore firmware writes ---- */
    case CANFD_CiTXQSTA:
    case CANFD_CiTXQUA:
        return;

    /* ---- TEF (stub) ---- */
    case CANFD_CiTEFCON:
        s->tefcon = apply_sci(s->tefcon, v32, sub);
        return;
    case CANFD_CiTEFSTA:
        s->tefsta = apply_sci(s->tefsta, v32, sub);
        return;
    case CANFD_CiTEFUA:
        return;

    /* ---- CiFIFOBA — Phase 3C stub: log and ignore ---- */
    case CANFD_CiFIFOBA:
        if (sub == 0) {
            qemu_log_mask(LOG_UNIMP,
                "pic32mk-canfd: CiFIFOBA write 0x%08x (Phase 3C stub, "
                "using fixed msg RAM base 0x%08x)\n",
                v32, s->msg_ram_phys);
        }
        return;

    default:
        break;
    }

    /*
     * ---- FIFO registers: base 0x170, stride 0x30 per FIFO (n=1..31)
     *   +0x00: CiFIFOCONn, +0x10: CiFIFOSTAn, +0x20: CiFIFOUAn ----
      */
    if (base_off >= CANFD_CiFIFOCON(1) && base_off <= CANFD_CiFIFOCON(31) + 0x20u) {
        int idx      = (int)((base_off - 0x170u) / 0x30u) + 1;
        int field    = (int)((base_off - 0x170u) % 0x30u);
        if (idx < 1 || idx > 31) {
            return;
        }

        if (field == 0x00) {
            /* CiFIFOCONn */
            uint32_t new_con = apply_sci(s->fifocon[idx], v32, sub);

            if (new_con & CANFD_FIFO_FRESET) {
                s->fifocon[idx] = new_con & ~CANFD_FIFO_FRESET;
                canfd_freset_fifo(s, idx);
                return;
            }

            bool uinc  = (new_con & CANFD_FIFO_UINC)  != 0u;
            bool txreq = (new_con & CANFD_FIFO_TXREQ)  != 0u;
            s->fifocon[idx] = new_con & ~(CANFD_FIFO_UINC | CANFD_FIFO_TXREQ);

            /*
             * First-time TX FIFO configuration (Harmony CAN1..4 init pattern):
             * firmware writes FIFOCONn with TXEN set; at this point the FIFO
             * is empty so it is "not full" and FIFOUA must point to slot 0.
             * Mirror the TXQ treatment: set TFNRFNIF=1 and initialise UA.
              */
            if ((s->fifocon[idx] & CANFD_FIFO_TXEN) && s->fifoua[idx] == 0) {
                s->fifosta[idx] |= CANFD_FIFOSTA_TFNRFNIF;
                s->fifoua[idx] = canfd_fifo_slot_ua(s, idx, 0);
            }

            /*
             * Clearing TFERFFIE (bit 4) acknowledges TX completion for this
             * FIFO — deassert its txif source bit.
              */
            if (!(s->fifocon[idx] & (1u << 4u))) {
                s->txif &= ~(1u << idx);
                canfd_update_irq(s);
            }
            if (uinc) {
                canfd_uinc_fifo(s, idx);
            }
            if (txreq && (s->fifocon[idx] & CANFD_FIFO_TXEN)) {
                canfd_process_tx(s, idx);
            }

            /*
             * For RX FIFOs, honour TFNRFNIE: if the firmware just toggled it,
             * update rxif to match the real hardware behaviour.
             * Enabling  → IRQ rises  (frame already in FIFO fires interrupt)
             * Disabling → IRQ falls  (flow-control)
              */
            if (!(s->fifocon[idx] & CANFD_FIFO_TXEN)) {
                bool new_ie   = (s->fifocon[idx] & CANFD_FIFO_TFNRFNIE) != 0;
                bool has_data = (s->fifosta[idx] & CANFD_FIFOSTA_TFNRFNIF) != 0;
                if (has_data && new_ie) {
                    s->rxif |= (1u << idx);
                } else {
                    s->rxif &= ~(1u << idx);
                }
                canfd_update_irq(s);
            }

        } else if (field == 0x10) {
            /* CiFIFOSTAn — firmware can clear status flags */
            s->fifosta[idx] = apply_sci(s->fifosta[idx], v32, sub);
            if (!(s->fifosta[idx] & CANFD_FIFOSTA_TFNRFNIF)) {
                if (s->fifo_count[idx] == 0) {
                    s->rxif &= ~(1u << idx);
                }
            }
            canfd_update_irq(s);
        }
        /* CiFIFOUA (+0x20) is hardware-owned, ignore writes */
        return;
    }

    /* ---- Filter control: stride 0x10 per register (r=0..7) ---- */
    if (base_off >= CANFD_CiFLTCON(0) && base_off <= CANFD_CiFLTCON(7)) {
        int reg = (int)((base_off - 0x740u) / 0x10u);
        s->fltcon[reg] = apply_sci(s->fltcon[reg], v32, sub);
        return;
    }

    /* ---- Filter object / mask — only writable in Config mode ---- */
    if (base_off >= CANFD_CiFLTOBJ(0) && base_off < 0x7D0u + 32u * 0x20u) {
        if (opmod != CANFD_OPMOD_CONFIG) {
            return;
        }
        if (base_off >= 0x7C0u && base_off < 0x7C0u + 32u * 0x20u) {
            int fn = (int)((base_off - 0x7C0u) / 0x20u);
            s->fltobj[fn] = apply_sci(s->fltobj[fn], v32, sub);
        } else if (base_off >= 0x7D0u) {
            int fn = (int)((base_off - 0x7D0u) / 0x20u);
            if (fn < 32) {
                s->mask[fn] = apply_sci(s->mask[fn], v32, sub);
            }
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk-canfd: unimplemented SFR write @ +0x%03" HWADDR_PRIx
                  " = 0x%08x\n", offset, v32);
}

static const MemoryRegionOps canfd_sfr_ops = {
    .read       = canfd_sfr_read,
    .write      = canfd_sfr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_canfd_reset(DeviceState *dev)
{
    PIC32MKCANFDState *s = PIC32MK_CANFD(dev);

    s->con     = CANFD_CON_RESET;    /* OPMOD=100 (Config), REQOP=100 */
    s->nbtcfg  = 0;
    s->dbtcfg  = 0;
    s->tdc     = 0;
    s->tbc     = 0;
    s->tscon   = 0;
    s->vec     = 0;
    s->cint    = 0;
    s->rxif    = 0;
    s->txif    = 0;
    s->rxovif  = 0;
    s->txreq   = 0;
    s->trec    = 0;
    s->tefcon  = 0;
    s->tefsta  = 0;
    s->tefua   = 0;
    s->txqcon  = 0;
    s->txqsta   = CANFD_TXQSTA_TXQNIF;  /* TXQNIF=1 (bit 0) — slot available on reset */
    s->txq_head  = 0;
    s->txq_tail  = 0;
    s->txq_count = 0;
    s->txqua    = 0;          /* will be computed when TXQEN is set */

    for (int i = 0; i < 32; i++) {
        s->fifocon[i]   = 0;
        s->fifosta[i]   = 0;
        s->fifoua[i]    = 0;
        s->fifo_head[i] = 0;
        s->fifo_tail[i] = 0;
        s->fifo_count[i] = 0;
        s->fltobj[i]    = 0;
        s->mask[i]      = 0;
    }
    for (int i = 0; i < 8; i++) {
        s->fltcon[i] = 0;
    }

    /* Clear message RAM */
    if (s->msg_ram_buf) {
        memset(s->msg_ram_buf, 0, PIC32MK_CAN_MSGRAM_SIZE);
    }

    /* Reset software bus buffer */
    s->bus_buf_head  = 0;
    s->bus_buf_tail  = 0;
    s->bus_buf_count = 0;

    qemu_irq_lower(s->irq);
}

static void pic32mk_canfd_instance_init(Object *obj)
{
    PIC32MKCANFDState *s = PIC32MK_CANFD(obj);
    object_property_add_link(obj, "canbus", TYPE_CAN_BUS,
                             (Object **)&s->canbus,
                             qdev_prop_allow_set_link_before_realize,
                             0);
}

static void pic32mk_canfd_realize(DeviceState *dev, Error **errp)
{
    PIC32MKCANFDState *s = PIC32MK_CANFD(dev);

    /* SFR region (index 0) */
    memory_region_init_io(&s->sfr_mmio, OBJECT(s), &canfd_sfr_ops, s,
                          "pic32mk-canfd-sfr", PIC32MK_CAN_SFR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->sfr_mmio);

    /* Message RAM backing store */
    s->msg_ram_buf = g_malloc0(PIC32MK_CAN_MSGRAM_SIZE);

    /* Message RAM region (index 1) — RAM-ptr so CPU can read/write freely */
    memory_region_init_ram_ptr(&s->msg_ram, OBJECT(s),
                               "pic32mk-canfd-msgram",
                               PIC32MK_CAN_MSGRAM_SIZE,
                               s->msg_ram_buf);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->msg_ram);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    /* Connect to virtual CAN bus if one was linked */
    s->bus_client.info    = &canfd_bus_client_info;
    s->bus_client.fd_mode = true;
    if (s->canbus) {
        if (can_bus_insert_client(s->canbus, &s->bus_client) < 0) {
            error_setg(errp, "pic32mk-canfd: can_bus_insert_client failed");
            return;
        }
    }
}

static void pic32mk_canfd_unrealize(DeviceState *dev)
{
    PIC32MKCANFDState *s = PIC32MK_CANFD(dev);
    can_bus_remove_client(&s->bus_client);
    g_free(s->msg_ram_buf);
    s->msg_ram_buf = NULL;
}

/*
 * Properties
 * -----------------------------------------------------------------------
 */

static const Property pic32mk_canfd_props[] = {
    DEFINE_PROP_UINT32("msg-ram-base", PIC32MKCANFDState, msg_ram_phys, 0),
    DEFINE_PROP_UINT32("instance-id", PIC32MKCANFDState, instance_id, 0),
};

/*
 * Class / type registration
 * -----------------------------------------------------------------------
 */

static void pic32mk_canfd_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize   = pic32mk_canfd_realize;
    dc->unrealize = pic32mk_canfd_unrealize;
    device_class_set_legacy_reset(dc, pic32mk_canfd_reset);
    device_class_set_props(dc, pic32mk_canfd_props);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "Microchip PIC32MK CAN FD controller";
}

static const TypeInfo pic32mk_canfd_info = {
    .name          = TYPE_PIC32MK_CANFD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKCANFDState),
    .instance_init = pic32mk_canfd_instance_init,
    .class_init    = pic32mk_canfd_class_init,
};

static void pic32mk_canfd_register_types(void)
{
    type_register_static(&pic32mk_canfd_info);
}

type_init(pic32mk_canfd_register_types)
