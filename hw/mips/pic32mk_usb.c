/*
 * Microchip PIC32MK USB OTG Full-Speed controller emulation
 * Datasheet: DS60001519E §25
 * Register addresses verified against p32mk1024mcm100.h (XC32 v4.60 pack).
 *
 * Phase 4A — register-file stub (complete)
 * Phase 4B — USB enumeration simulation + CDC TX chardev output:
 *   • QEMUTimer drives the EP0 state machine (USB Reset → enumerate)
 *   • SETUP packet injection via BDT write + TRNIF interrupt
 *   • CDC TX polling: EP1-IN BDT entry harvested → chardev write
 *   • chardev property "chardev" exposes CDC output as host PTY/socket
 *
 * BDT layout (PIC32MK device mode, ping-pong off / PPBRST):
 *   Each endpoint has 4 BDT entries (RX-even, RX-odd, TX-even, TX-odd).
 *   Entry size = 8 bytes (4-byte ctrl word + 4-byte buffer address).
 *   EP0-OUT-even = offset 0x00, EP0-IN-even = offset 0x10,
 *   EP1-OUT-even = offset 0x20, EP1-IN-even = offset 0x30.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/mips/pic32mk.h"
#include "hw/mips/pic32mk_usb.h"
#include "exec/cpu-common.h"   /* cpu_physical_memory_read/write */
#include "chardev/char-fe.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"

/*
 * Helpers
 * -----------------------------------------------------------------------
 */

/*
 * Apply SET / CLR / INV operation.
 * sub = addr & 0xF: 0 → write, 4 → CLR, 8 → SET, C → INV
 */
static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0x0:
        *reg  = val;
        break;
    case 0x4:
        *reg &= ~val;
        break;
    case 0x8:
        *reg |= val;
        break;
    case 0xC:
        *reg ^= val;
        break;
    }
}

/*
 * Apply write-1-clear (W1C) operation.
 */
static void apply_w1c(uint32_t *reg, uint32_t val, int sub)
{
    if (sub == 0x0 || sub == 0x4) {
        *reg &= ~val;
    }
}

static void usb_update_irq(PIC32MKUSBState *s)
{
    /*
     * SESSION_VALID (OTGIR bit 3 / SESVDIF):
     *
     * On real PIC32MK hardware, SESVDIF is edge-triggered: it fires once when
     * SESVD transitions.  To prevent an infinite ISR loop (the Harmony ISR
     * clears SESVDIF but VBUS stays valid), we use an edge-latch flag:
     * sesvd_edge_latched is set when SESVDIF is first asserted, and cleared
     * when firmware W1C-clears SESVDIF from OTGIR.  We only include SESVDIF
     * in the IRQ level calculation while the edge latch is active.
     */
    uint32_t otg_pending = s->otgir & s->otgie;
    if (!s->sesvd_edge_latched) {
        otg_pending &= ~USB_OTG_IR_SESSION_VALID;
    }

    bool fire = ((s->uir  & s->uie)  != 0)
             || ((s->ueir & s->ueie) != 0)
             || (otg_pending != 0);
    qemu_set_irq(s->irq, fire ? 1 : 0);
}

/*
 * USTAT FIFO helpers (PIC32MK has a 4-deep hardware FIFO)
 *
 * On real hardware, each completed transaction pushes an USTAT value onto
 * the FIFO and asserts TRNIF.  When firmware W1C-clears TRNIF, the FIFO
 * pops; if more entries remain, TRNIF is re-asserted immediately.
 * Without this FIFO, two transactions completing in quick succession
 * (e.g. chardev RX + timer TX poll) overwrite the single USTAT register,
 * causing the firmware to miss one transaction entirely — the root cause
 * of the write stall.
 * -----------------------------------------------------------------------
 */

static void usb_stat_fifo_push(PIC32MKUSBState *s, uint32_t stat_val)
{
    if (s->stat_fifo_count >= 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk_usb: USTAT FIFO overflow (dropped 0x%02x)\n",
                      stat_val);
        return;
    }
    s->stat_fifo[s->stat_fifo_head] = stat_val;
    s->stat_fifo_head = (s->stat_fifo_head + 1) & 3;
    s->stat_fifo_count++;
    /* Expose the front of the FIFO via the USTAT register */
    s->ustat = s->stat_fifo[s->stat_fifo_tail];
    s->uir |= USB_IR_TRNIF;
}

/*
 * Pop the front entry.  Called when firmware W1C-clears TRNIF.
 * If more entries remain, re-assert TRNIF with the next USTAT value.
 */
static void usb_stat_fifo_pop(PIC32MKUSBState *s)
{
    if (s->stat_fifo_count == 0) {
        return;
    }
    s->stat_fifo_tail = (s->stat_fifo_tail + 1) & 3;
    s->stat_fifo_count--;
    if (s->stat_fifo_count > 0) {
        s->ustat = s->stat_fifo[s->stat_fifo_tail];
        s->uir |= USB_IR_TRNIF;
    }
}

/*
 * BDT helpers
 * -----------------------------------------------------------------------
 */

static hwaddr usb_bdt_phys(PIC32MKUSBState *s)
{
    /*
     * BDT base = (BDTP3[7:0] << 24) | (BDTP2[7:0] << 16) | (BDTP1[7:1] << 8)
     * BDTP1 bits [7:1] map to BDT address bits [15:9].
      */
    return ((hwaddr)(s->bdtp3 & 0xFFu) << 24)
         | ((hwaddr)(s->bdtp2 & 0xFFu) << 16)
         | ((hwaddr)(s->bdtp1 & 0xFEu) << 8);
}

/*
 * Inject an 8-byte SETUP packet into the EP0-OUT-even BDT buffer.
 * The BDT entry at offset 0x00 from BDT base must be UOWN=1 (firmware armed).
 * On success: writes the SETUP data to the buffer, clears UOWN, sets
 * TOK_PID=SETUP (0xD) in byte[0] bits[5:2]=0x34, bc=8 in shortWord[1]
 * (bits[31:16]), sets ustat=EP0/OUT/EVEN, fires TRNIF.
 *
 * BDT 32-bit control word layout (Harmony DRV_USBFS_BDT_ENTRY union):
 *   bits[ 7: 0] = byte[0]:  UOWN(7), DATA01(6), DTSEN(4), BSTALL(2), TOK_PID[5:2]
 *   bits[15: 8] = byte[1]:  (reserved / padding)
 *   bits[31:16] = shortWord[1]: byte count (BC)
 *   word[1]                  = buffer physical address
 *
 * Harmony TRNIF switch uses (byte[0] & 0x3C):
 *   0x34 → SETUP (TOK_PID=0xD)
 *   0x04 → OUT   (TOK_PID=0x1)
 *   0x24 → IN    (TOK_PID=0x9)
 */
static bool usb_inject_setup(PIC32MKUSBState *s, const uint8_t setup[8])
{
    hwaddr bdt = usb_bdt_phys(s);
    if (!bdt) {
        return false;
    }

    /* Check both ping-pong entries for EP0-OUT (even=BDT+0, odd=BDT+8). */
    for (int ppbi = 0; ppbi < 2; ppbi++) {
        hwaddr out_entry = bdt + (ppbi ? 8u : 0u);
        uint8_t entry[8];
        cpu_physical_memory_read(out_entry, entry, 8);

        uint32_t ctrl = le32_to_cpu(*(uint32_t *)entry);
        uint32_t addr = le32_to_cpu(*(uint32_t *)(entry + 4));

        if (!(ctrl & BDT_UOWN) || !addr) {
            continue;   /* not armed — try other ping-pong */
        }

        /* Write SETUP packet into the EP0-OUT buffer */
        cpu_physical_memory_write(addr, setup, 8);

        /*
         * Return BDT entry to firmware:
         *   byte[0] = 0x34: TOK_PID=SETUP(0xD) in bits[5:2], UOWN=0
         *   shortWord[1] = 8: byte count
         */
        ctrl = 0x00000034u | (8u << 16);
        *(uint32_t *)entry = cpu_to_le32(ctrl);
        cpu_physical_memory_write(out_entry, entry, 8);

        /* UxSTAT: EP=0, DIR=OUT(0), PPBI=ppbi */
        usb_stat_fifo_push(s, (uint32_t)(ppbi ? 0x04u : 0x00u));
        usb_update_irq(s);
        return true;
    }
    return false;   /* neither entry armed yet */
}

/*
 * Accept an EP0-IN response from the firmware: read the data the firmware
 * placed in EP0-IN-even (BDT offset 0x10), set TOK_PID=IN in byte[0],
 * clear UOWN, preserve bc in shortWord[1], fire TRNIF.
 * Returns true if an IN entry was accepted, false if not armed yet.
 */
static bool usb_accept_ep0_in(PIC32MKUSBState *s)
{
    hwaddr bdt = usb_bdt_phys(s);
    if (!bdt) {
        return false;
    }

    /* Check both ping-pong entries for EP0-IN (even=BDT+0x10, odd=BDT+0x18). */
    for (int ppbi = 0; ppbi < 2; ppbi++) {
        hwaddr in_entry = bdt + 0x10u + (ppbi ? 8u : 0u);
        uint8_t entry[8];
        cpu_physical_memory_read(in_entry, entry, 8);

        uint32_t ctrl = le32_to_cpu(*(uint32_t *)entry);
        if (!(ctrl & BDT_UOWN)) {
            continue;   /* not armed */
        }

        /*
         * Return BDT entry to firmware:
         *   byte[0] = 0x24: TOK_PID=IN(0x9), UOWN=0
         *   shortWord[1]: preserve bc
         */
        ctrl = (ctrl & 0xFFFF0000u) | 0x24u;
        *(uint32_t *)entry = cpu_to_le32(ctrl);
        cpu_physical_memory_write(in_entry, entry, 8);

        /* UxSTAT: EP=0, DIR=IN(bit3), PPBI=ppbi(bit2) */
        usb_stat_fifo_push(s, 0x08u | (uint32_t)(ppbi ? 0x04u : 0x00u));
        usb_update_irq(s);
        return true;
    }
    return false;
}

/*
 * Simulate the STATUS phase OUT for a control read (host→device ZLP).
 * Write EP0-OUT-even BDT entry: TOK_PID=OUT(0x1)→byte[0]=0x04, UOWN=0,
 * bc=0 in shortWord[1].  Then fire TRNIF so the Harmony TRNIF handler
 * hits case 0x04, sees shortWord[1]=0 < maxPacketSize, marks the IRP
 * complete, invokes the callback, and re-arms EP0-OUT for the next SETUP.
 */
static void usb_send_status_out(PIC32MKUSBState *s)
{
    hwaddr bdt = usb_bdt_phys(s);
    if (!bdt) {
        return;
    }

    /*
     * Find the armed EP0-OUT ping-pong entry (even=BDT+0, odd=BDT+8).
     * Write: byte[0]=0x04 (TOK_PID=OUT), UOWN=0, shortWord[1]=0 (ZLP).
     */
    for (int ppbi = 0; ppbi < 2; ppbi++) {
        hwaddr out_entry = bdt + (ppbi ? 8u : 0u);
        uint8_t entry[8];
        cpu_physical_memory_read(out_entry, entry, 8);
        uint32_t ctrl = le32_to_cpu(*(uint32_t *)entry);

        if (ctrl & BDT_UOWN) {
            ctrl = 0x00000004u;   /* TOK_PID=OUT, UOWN=0, bc=0 */
            *(uint32_t *)entry = cpu_to_le32(ctrl);
            cpu_physical_memory_write(out_entry, entry, 8);
            usb_stat_fifo_push(s, (uint32_t)(ppbi ? 0x04u : 0x00u));
            usb_update_irq(s);
            return;
        }
    }

    /* Neither armed — fire even anyway (fallback; should not happen) */
    uint8_t entry[8];
    cpu_physical_memory_read(bdt, entry, 8);
    *(uint32_t *)entry = cpu_to_le32(0x00000004u);
    cpu_physical_memory_write(bdt, entry, 8);
    usb_stat_fifo_push(s, USB_STAT_EP0_OUT_EVEN);
    usb_update_irq(s);
}

/*
 * Chardev RX callbacks (CDC Host→Device path, EP2-OUT)
 * -----------------------------------------------------------------------
 */

/*
 * Returns 64 (max bulk packet) when enumeration is done and EP2-OUT BDT
 * is armed by the firmware (UOWN=1). Returns 0 otherwise so the chardev
 * layer does not deliver data before the firmware is ready.
 */
static int usb_chr_can_receive(void *opaque)
{
    PIC32MKUSBState *s = opaque;
    if (s->ep0_sim != EP0_SIM_DONE) {
        return 0;
    }
    hwaddr bdt = usb_bdt_phys(s);
    if (!bdt) {
        return 0;
    }
    /* EP2-OUT-even = BDT+0x40, EP2-OUT-odd = BDT+0x48 */
    for (int ppbi = 0; ppbi < 2; ppbi++) {
        uint8_t entry[4];
        cpu_physical_memory_read(bdt + 0x40u + (ppbi ? 8u : 0u), entry, 4);
        uint32_t ctrl = le32_to_cpu(*(uint32_t *)entry);
        if (ctrl & BDT_UOWN) {
            return 64;
        }
    }
    return 0;
}

/*
 * Called by the QEMU chardev layer when the host sends data over the PTY/socket.
 * Writes the payload into the firmware's EP2-OUT buffer, returns the BDT entry
 * to firmware (UOWN=0, TOK_PID=OUT, bc=n), and fires TRNIF so the Harmony ISR
 * picks it up via USB_ReadByte() → circular_buf_get().
 */
static void usb_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    PIC32MKUSBState *s = opaque;
    hwaddr bdt = usb_bdt_phys(s);
    if (!bdt) {
        return;
    }

    for (int ppbi = 0; ppbi < 2; ppbi++) {
        hwaddr ep2_out = bdt + 0x40u + (ppbi ? 8u : 0u);
        uint8_t entry[8];
        cpu_physical_memory_read(ep2_out, entry, 8);

        uint32_t ctrl = le32_to_cpu(*(uint32_t *)entry);
        uint32_t addr = le32_to_cpu(*(uint32_t *)(entry + 4));

        if (!(ctrl & BDT_UOWN) || !addr) {
            continue;   /* not armed — try other ping-pong */
        }

        int n = MIN(size, 64);
        cpu_physical_memory_write(addr, buf, n);

        /*
         * Return BDT entry to firmware:
         *   byte[0] = 0x04: TOK_PID=OUT(0x1) in bits[5:2], UOWN=0
         *   shortWord[1] = n: byte count received
         */
        ctrl = 0x00000004u | ((uint32_t)n << 16);
        *(uint32_t *)entry = cpu_to_le32(ctrl);
        cpu_physical_memory_write(ep2_out, entry, 8);

        /* UxSTAT: EP=2(bits[7:4]=0x20), DIR=OUT(bit3=0), PPBI=ppbi(bit2) */
        usb_stat_fifo_push(s, 0x20u | (uint32_t)(ppbi ? 0x04u : 0x00u));
        usb_update_irq(s);
        return;
    }
}

/*
 * Poll the CDC TX endpoint (EP2-IN) and the CDC notification endpoint (EP1-IN).
 * If the firmware has queued data (UOWN=1), drain it, release the BDT entry,
 * and fire TRNIF so the firmware can refill.
 *
 * EP1-IN (BDT+0x30): CDC Serial State Notification — data discarded (host-side
 * notification only), but BDT must be returned to prevent firmware stall.
 * EP2-IN (BDT+0x50): CDC bulk TX — data forwarded to chardev.
 */
static void usb_check_cdc_bdt(PIC32MKUSBState *s)
{
    hwaddr bdt = usb_bdt_phys(s);
    if (!bdt) {
        return;
    }

    /*
     * EP1-IN = CDC Serial State Notification (interrupt IN, 16 bytes max).
     * BDT: EP1-IN-even = BDT+0x30, EP1-IN-odd = BDT+0x38.
     * Drain and return to firmware without forwarding to chardev.
     */
    for (int ppbi = 0; ppbi < 2; ppbi++) {
        hwaddr ep1_in = bdt + 0x30u + (ppbi ? 8u : 0u);
        uint8_t entry[8];
        cpu_physical_memory_read(ep1_in, entry, 8);

        uint32_t ctrl = le32_to_cpu(*(uint32_t *)entry);
        if (!(ctrl & BDT_UOWN)) {
            continue;
        }

        /* Return BDT entry: TOK_PID=IN, UOWN=0, preserve bc */
        ctrl = (ctrl & 0xFFFF0000u) | 0x24u;
        *(uint32_t *)entry = cpu_to_le32(ctrl);
        cpu_physical_memory_write(ep1_in, entry, 8);

        /* UxSTAT: EP=1(bits[7:4]=0x10), DIR=IN(bit3=0x08), PPBI=ppbi(bit2) */
        usb_stat_fifo_push(s, 0x18u | (uint32_t)(ppbi ? 0x04u : 0x00u));
        usb_update_irq(s);
        break;   /* one notification per tick is enough */
    }

    /*
     * EP2-IN = CDC bulk TX.  BDT: EP2-IN-even = BDT+0x50, EP2-IN-odd = BDT+0x58.
     * Forward data to chardev backend (PTY / socket / file).
     */
    for (int ppbi = 0; ppbi < 2; ppbi++) {
        hwaddr ep2_in = bdt + 0x50u + (ppbi ? 8u : 0u);
        uint8_t entry[8];
        cpu_physical_memory_read(ep2_in, entry, 8);

        uint32_t ctrl = le32_to_cpu(*(uint32_t *)entry);
        if (!(ctrl & BDT_UOWN)) {
            continue;
        }

        uint32_t bc   = ctrl >> 16;
        uint32_t addr = le32_to_cpu(*(uint32_t *)(entry + 4));

        if (bc > 0 && addr) {
            uint8_t txbuf[64];
            bc = MIN(bc, sizeof(txbuf));
            cpu_physical_memory_read(addr, txbuf, bc);
            if (qemu_chr_fe_backend_connected(&s->chr)) {
                qemu_chr_fe_write_all(&s->chr, txbuf, bc);
            }
        }

        /* Return BDT entry: TOK_PID=IN, UOWN=0, preserve bc */
        ctrl = (ctrl & 0xFFFF0000u) | 0x24u;
        *(uint32_t *)entry = cpu_to_le32(ctrl);
        cpu_physical_memory_write(ep2_in, entry, 8);

        /* UxSTAT: EP=2(bits[7:4]=0x20), DIR=IN(bit3=0x08), PPBI=ppbi(bit2) */
        usb_stat_fifo_push(s, 0x28u | (uint32_t)(ppbi ? 0x04u : 0x00u));
        usb_update_irq(s);
        return;   /* drain one packet per tick; FIFO handles concurrency */
    }
}

/*
 * EP0 enumeration timer
 * -----------------------------------------------------------------------
 */

static void usb_timer_cb(void *opaque)
{
    PIC32MKUSBState *s = opaque;

    /* Standard USB SETUP packets for enumeration sequence */
    static const uint8_t setup_get_dev_desc[8] = {
        0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 18, 0x00
    };  /* GET_DESCRIPTOR(Device, length=18) */

    static const uint8_t setup_set_address[8] = {
        0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
    };  /* SET_ADDRESS(1) */

    static const uint8_t setup_get_cfg_desc[8] = {
        0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 67, 0x00
    };  /* GET_DESCRIPTOR(Configuration, length=67) */

    static const uint8_t setup_set_config[8] = {
        0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
    };  /* SET_CONFIGURATION(1) */

    switch (s->ep0_sim) {

    case EP0_SIM_IDLE:
        /* Do nothing until USBPWR is set */
        break;

    case EP0_SIM_RESET:
        /*
         * Wait until firmware enables URSTIE (bit 0) in UIE.
         * The UIE write handler fires URSTIF immediately when URSTIE is first
         * enabled (edge-detected).  This path is a poll fallback: if URSTIE
         * is already set by the time we arrive here, fire now; otherwise retry
         * every 5ms.  The 50ms initial delay usually isn't long enough for the
         * Harmony stack to complete Init→Attach→EnableInterrupts.
          */
        if (!(s->uie & USB_IR_URSTIF)) {
            /*
             * SESSION_VALID fallback: if the initial SESVDIF pulse was missed
             * (e.g. IEC not enabled yet when pulse fired), re-latch SESVDIF
             * every few retries so the EVIC can deliver the interrupt once
             * IEC is enabled.
              */
            s->sesvd_retry_count++;
            /*
             * Only re-latch if firmware has never acknowledged SESVDIF.
             * Once sesvd_acked is true, the Attach→URSTIE chain has
             * started and retries would cause an IRQ storm.
              */
            if (!s->sesvd_acked &&
                (s->sesvd_retry_count % 3) == 0 &&
                (s->otgstat & USB_OTG_IR_SESSION_VALID) &&
                (s->otgie & USB_OTG_IR_SESSION_VALID)) {
                s->otgir |= USB_OTG_IR_SESSION_VALID;
                s->sesvd_edge_latched = true;
                usb_update_irq(s);
            }
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
            return;
        }
        s->uir |= USB_IR_URSTIF;
        usb_update_irq(s);
        s->ep0_sim = EP0_SIM_GET_DEV_DESC;
        timer_mod(s->usb_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10 * SCALE_MS);
        return;

    case EP0_SIM_GET_DEV_DESC:
        if (usb_inject_setup(s, setup_get_dev_desc)) {
            s->ep0_sim = EP0_SIM_WAIT_DEV_DESC;
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
        } else {
            /* Retry every 2 ms until BDT is armed */
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 2 * SCALE_MS);
        }
        return;

    case EP0_SIM_WAIT_DEV_DESC:
        /* Consume the EP0-IN response(s) — may be multi-packet (64+3 bytes) */
        if (usb_accept_ep0_in(s)) {
            /* Give firmware 2ms to re-arm IN or finish */
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 2 * SCALE_MS);
        } else {
            /* IN response not ready yet, or we just consumed last packet */
            usb_send_status_out(s);  /* status phase complete */
            s->ep0_sim = EP0_SIM_SET_ADDRESS;
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
        }
        return;

    case EP0_SIM_SET_ADDRESS:
        if (usb_inject_setup(s, setup_set_address)) {
            s->uaddr = 1;   /* pretend host acknowledged new address */
            s->ep0_sim = EP0_SIM_WAIT_ADDRESS;
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
        } else {
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 2 * SCALE_MS);
        }
        return;

    case EP0_SIM_WAIT_ADDRESS:
        /* Consume status-phase ZLP IN from SET_ADDRESS */
        if (usb_accept_ep0_in(s)) {
            s->ep0_sim = EP0_SIM_GET_CFG_DESC;
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
        } else {
            /* Retry */
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 2 * SCALE_MS);
        }
        return;

    case EP0_SIM_GET_CFG_DESC:
        if (usb_inject_setup(s, setup_get_cfg_desc)) {
            s->ep0_sim = EP0_SIM_WAIT_CFG_DESC;
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
        } else {
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 2 * SCALE_MS);
        }
        return;

    case EP0_SIM_WAIT_CFG_DESC:
        /* May be multi-packet; keep accepting until no more IN pending */
        if (usb_accept_ep0_in(s)) {
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 2 * SCALE_MS);
        } else {
            usb_send_status_out(s);  /* status phase */
            s->ep0_sim = EP0_SIM_SET_CONFIG;
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
        }
        return;

    case EP0_SIM_SET_CONFIG:
        if (usb_inject_setup(s, setup_set_config)) {
            s->ep0_sim = EP0_SIM_WAIT_CONFIG;
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
        } else {
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 2 * SCALE_MS);
        }
        return;

    case EP0_SIM_WAIT_CONFIG:
        /* Consume status-phase ZLP IN from SET_CONFIGURATION */
        if (usb_accept_ep0_in(s)) {
            s->configured = true;
            s->ep0_sim = EP0_SIM_DONE;
            qemu_log_mask(LOG_UNIMP, "pic32mk_usb: USB enumeration DONE\n");
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 5 * SCALE_MS);
        } else {
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 2 * SCALE_MS);
        }
        return;

    case EP0_SIM_DONE:
        /*
         * Poll CDC TX every 1 ms (was 5 ms — too slow for bootloader
         * throughput; each WRITE command needs a response drained from
         * EP2-IN before the firmware can process the next one).
          */
        usb_check_cdc_bdt(s);
        /* Advance the frame counter (1 frame = 1 ms at full-speed) */
        {
            uint32_t frm = ((s->ufrmh & 0x07u) << 8) | s->ufrml;
            frm = (frm + 1) & 0x7FFu;
            s->ufrml = frm & 0xFFu;
            s->ufrmh = (frm >> 8) & 0x07u;
        }
        timer_mod(s->usb_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1 * SCALE_MS);
        return;
    }
}

/*
 * MMIO read
 * -----------------------------------------------------------------------
 */

static uint64_t usb_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKUSBState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xFu;   /* strip sub-register bits */

    switch (base) {
    /* OTG block */
    case PIC32MK_UxOTGIR:
        return s->otgir;
    case PIC32MK_UxOTGIE:
        return s->otgie;
    case PIC32MK_UxOTGSTAT:
        return s->otgstat;
    case PIC32MK_UxOTGCON:
        return s->otgcon;
    case PIC32MK_UxPWRC:
        return s->pwrc;

    /* Core registers */
    case PIC32MK_UxIR:
        return s->uir;
    case PIC32MK_UxIE:
        return s->uie;
    case PIC32MK_UxEIR:
        return s->ueir;
    case PIC32MK_UxEIE:
        return s->ueie;
    case PIC32MK_UxSTAT:
        return s->ustat;
    case PIC32MK_UxCON:
        return s->ucon;
    case PIC32MK_UxADDR:
        return s->uaddr;
    case PIC32MK_UxBDTP1:
        return s->bdtp1;
    case PIC32MK_UxFRML:
        return s->ufrml;
    case PIC32MK_UxFRMH:
        return s->ufrmh;
    case PIC32MK_UxTOK:
        return s->utok;
    case PIC32MK_UxSOF:
        return s->usof;
    case PIC32MK_UxBDTP2:
        return s->bdtp2;
    case PIC32MK_UxBDTP3:
        return s->bdtp3;
    case PIC32MK_UxCNFG1:
        return s->cnfg1;

    default:
        break;
    }

    /* Endpoint control registers UxEP0–UxEP15 */
    if (base >= PIC32MK_UxEP_BASE &&
        base < PIC32MK_UxEP_BASE + PIC32MK_USB_NEPS * PIC32MK_UxEP_STRIDE) {
        unsigned ep = (base - PIC32MK_UxEP_BASE) / PIC32MK_UxEP_STRIDE;
        return s->uep[ep];
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_usb: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

/*
 * MMIO write
 * -----------------------------------------------------------------------
 */

static void usb_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKUSBState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xFu;
    int    sub  = (int)(addr & 0xFu);
    uint32_t v  = (uint32_t)val;

    switch (base) {

    /* ----- OTG block ----- */
    case PIC32MK_UxOTGIR:
        /*
         * Clear the edge latch before W1C so that usb_update_irq() deasserts
         * the IRQ line when SESVDIF is cleared by firmware.
          */
        if (v & USB_OTG_IR_SESSION_VALID) {
            s->sesvd_edge_latched = false;
            s->sesvd_acked = true;   /* firmware processed SESSION_VALID */
        }
        apply_w1c(&s->otgir, v, sub);
        usb_update_irq(s);
        return;

    case PIC32MK_UxOTGIE: {
        uint32_t old_otgie = s->otgie;
        apply_sci(&s->otgie, v, sub);
        /*
         * SESSION_VALID edge simulation:
         * Latch SESVDIF ONLY on the 0→1 transition of the SESSION_VALID enable
         * bit, and only if firmware hasn't already acknowledged it.  Without
         * edge detection, any OTGIE write (e.g. Harmony's Attach() enabling
         * all OTG ints from inside the SESSION_VALID ISR) would re-latch
         * SESVDIF and cause an infinite ISR loop.
          */
        if (!(old_otgie & USB_OTG_IR_SESSION_VALID) &&
            (s->otgie & USB_OTG_IR_SESSION_VALID) &&
            (s->otgstat & USB_OTG_IR_SESSION_VALID) &&
            !s->sesvd_acked) {
            s->otgir |= USB_OTG_IR_SESSION_VALID;
            s->sesvd_edge_latched = true;
            fprintf(stderr, "pic32mk_usb: SESSION_VALID latched → IRQ (persistent)\n");
        }
        usb_update_irq(s);
        return;
    }

    case PIC32MK_UxOTGSTAT:
        return;   /* read-only */

    case PIC32MK_UxOTGCON:
        apply_sci(&s->otgcon, v, sub);
        return;

    case PIC32MK_UxPWRC: {
        uint32_t old_pwrc = s->pwrc;
        apply_sci(&s->pwrc, v, sub);
        /* When USBPWR first asserted: schedule USB Reset after 50 ms */
        if ((s->pwrc & USB_PWRC_USBPWR) && !(old_pwrc & USB_PWRC_USBPWR)
            && s->ep0_sim == EP0_SIM_IDLE) {
            fprintf(stderr, "pic32mk_usb: USBPWR set — scheduling USB Reset in 50ms\n");
            s->ep0_sim = EP0_SIM_RESET;
            timer_mod(s->usb_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 50 * SCALE_MS);
        }
        return;
    }

    /* ----- Core interrupt registers ----- */
    case PIC32MK_UxIR:
        /*
         * TRNIF W1C: pop the USTAT FIFO.  On real PIC32MK hardware, clearing
         * TRNIF advances the 4-deep STAT FIFO.  If more entries remain,
         * TRNIF is re-asserted immediately with the next USTAT value, so the
         * Harmony ISR's while(TRNIF) loop processes all queued transactions.
         */
        if (v & USB_IR_TRNIF) {
            usb_stat_fifo_pop(s);
        }
        apply_w1c(&s->uir, v, sub);
        /*
         * Opportunistic CDC TX check: firmware just cleared TRNIF, meaning it
         * finished processing a transaction.  If it also armed EP2-IN (TX
         * response) during that processing, drain it NOW instead of waiting
         * for the next timer tick.  This brings TX latency close to zero.
         */
        if (s->configured) {
            usb_check_cdc_bdt(s);
        }
        usb_update_irq(s);
        return;

    case PIC32MK_UxIE: {
        uint32_t old_uie = s->uie;
        apply_sci(&s->uie, v, sub);
        /*
         * When firmware enables URSTIE (bit 0) for the first time and the EP0
         * state machine is armed (USBPWR was set earlier), fire URSTIF now.
         * On real hardware the host drives USB Reset after D+ pullup is seen.
         * We use the same edge-detection pattern as SESSION_VALID: pulse assert
         * + deassert so EVIC latches IFS exactly once, then let usb_update_irq
         * handle the persistent URSTIF level (UIR & UIE != 0).
          */
        if (!(old_uie & USB_IR_URSTIF) && (s->uie & USB_IR_URSTIF)
            && s->ep0_sim == EP0_SIM_RESET) {
            fprintf(stderr, "pic32mk_usb: URSTIE enabled → firing USB Reset\n");
            s->uir |= USB_IR_URSTIF;
        }
        usb_update_irq(s);
        return;
    }

    case PIC32MK_UxEIR:
        apply_w1c(&s->ueir, v, sub);
        usb_update_irq(s);
        return;

    case PIC32MK_UxEIE:
        apply_sci(&s->ueie, v, sub);
        usb_update_irq(s);
        return;

    case PIC32MK_UxSTAT:
        return;   /* read-only */

    case PIC32MK_UxCON:
        apply_sci(&s->ucon, v, sub);
        return;

    case PIC32MK_UxADDR:
        apply_sci(&s->uaddr, v, sub);
        return;

    case PIC32MK_UxBDTP1:
        apply_sci(&s->bdtp1, v, sub);
        return;

    case PIC32MK_UxFRML:
    case PIC32MK_UxFRMH:
        return;   /* read-only */

    case PIC32MK_UxTOK:
        apply_sci(&s->utok, v, sub);
        return;

    case PIC32MK_UxSOF:
        apply_sci(&s->usof, v, sub);
        return;

    case PIC32MK_UxBDTP2:
        apply_sci(&s->bdtp2, v, sub);
        return;

    case PIC32MK_UxBDTP3:
        apply_sci(&s->bdtp3, v, sub);
        return;

    case PIC32MK_UxCNFG1:
        apply_sci(&s->cnfg1, v, sub);
        return;

    default:
        break;
    }

    /* Endpoint control registers */
    if (base >= PIC32MK_UxEP_BASE &&
        base < PIC32MK_UxEP_BASE + PIC32MK_USB_NEPS * PIC32MK_UxEP_STRIDE) {
        unsigned ep = (base - PIC32MK_UxEP_BASE) / PIC32MK_UxEP_STRIDE;
        apply_sci(&s->uep[ep], v, sub);
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_usb: unimplemented write @ 0x%04" HWADDR_PRIx
                  " = 0x%08x\n", addr, v);
}

static const MemoryRegionOps usb_ops = {
    .read       = usb_read,
    .write      = usb_write,
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

static void pic32mk_usb_reset(DeviceState *dev)
{
    PIC32MKUSBState *s = PIC32MK_USB(dev);

    /* Reset values: all zero unless specified in DS60001519E §25 */
    s->otgir   = 0;
    s->otgie   = 0;
    /*
     * SESVD (bit 3): simulate VBUS present from power-on.
     * The Harmony driver polls PLIB_USB_OTG_SessionValid() (reads SESVD) to
     * decide whether to call USB_DEVICE_Attach().  If SESVD=0 at boot the
     * driver never calls Attach(), USBPWR is never set, and our timer-based
     * EP0 simulation never starts — a permanent deadlock.  Pre-setting SESVD
     * breaks the cycle: firmware sees VBUS valid, calls Attach(), sets USBPWR,
     * and our EP0 timer fires as expected.
      */
    s->otgstat = 0x08u;
    s->otgcon  = 0;
    s->pwrc    = 0;
    s->uir     = 0;
    s->uie     = 0;
    s->ueir    = 0;
    s->ueie    = 0;
    s->ustat   = 0;
    s->ucon    = 0;
    s->uaddr   = 0;
    s->bdtp1   = 0;
    s->bdtp2   = 0;
    s->bdtp3   = 0;
    s->ufrml   = 0;
    s->ufrmh   = 0;
    s->utok    = 0;
    s->usof    = 0x4Bu;
    s->cnfg1   = 0;
    memset(s->uep, 0, sizeof(s->uep));

    s->ep0_sim   = EP0_SIM_IDLE;
    s->configured = false;
    s->sesvd_edge_latched = false;
    s->sesvd_acked = false;
    s->sesvd_retry_count = 0;

    /* USTAT FIFO reset */
    memset(s->stat_fifo, 0, sizeof(s->stat_fifo));
    s->stat_fifo_head  = 0;
    s->stat_fifo_tail  = 0;
    s->stat_fifo_count = 0;

    if (s->usb_timer) {
        timer_del(s->usb_timer);
    }

    qemu_set_irq(s->irq, 0);
}

static void pic32mk_usb_init(Object *obj)
{
    PIC32MKUSBState *s = PIC32MK_USB(obj);

    memory_region_init_io(&s->sfr_mmio, obj, &usb_ops, s,
                          TYPE_PIC32MK_USB, PIC32MK_USB_SFR_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->sfr_mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    s->usb_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, usb_timer_cb, s);
    s->ep0_sim   = EP0_SIM_IDLE;
    s->configured = false;
    s->sesvd_edge_latched = false;
    s->sesvd_acked = false;
    s->sesvd_retry_count = 0;
    s->stat_fifo_head  = 0;
    s->stat_fifo_tail  = 0;
    s->stat_fifo_count = 0;
}

static void pic32mk_usb_realize(DeviceState *dev, Error **errp)
{
    PIC32MKUSBState *s = PIC32MK_USB(dev);
    if (qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_chr_fe_set_handlers(&s->chr,
                                 usb_chr_can_receive,
                                 usb_chr_receive,
                                 NULL, NULL,
                                 s, NULL, true);
    }
}

static const Property pic32mk_usb_properties[] = {
    DEFINE_PROP_CHR("chardev", PIC32MKUSBState, chr),
};

static void pic32mk_usb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_usb_reset);
    device_class_set_props(dc, pic32mk_usb_properties);
    dc->realize = pic32mk_usb_realize;
    dc->desc = "PIC32MK USB OTG Full-Speed";
}

static const TypeInfo pic32mk_usb_info = {
    .name          = TYPE_PIC32MK_USB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKUSBState),
    .instance_init = pic32mk_usb_init,
    .class_init    = pic32mk_usb_class_init,
};

static void pic32mk_usb_register_types(void)
{
    type_register_static(&pic32mk_usb_info);
}

type_init(pic32mk_usb_register_types)
