/*
 * Microchip PIC32MK USB OTG Full-Speed controller — device model header
 * Datasheet: DS60001519E §25; register addresses from p32mk1024mcm100.h
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MIPS_PIC32MK_USB_H
#define HW_MIPS_PIC32MK_USB_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"

/*
 * Register offsets from the USB instance SFR base (e.g. 0xBF889000)
 * Derived from p32mk1024mcm100.h register addresses.
 * All registers with CLR/SET/INV aliases follow the standard PIC32 convention
 * (+4 = CLR, +8 = SET, +C = INV), except where noted.
 * -----------------------------------------------------------------------
 */

/* OTG registers */
#define PIC32MK_UxOTGIR     0x040u  /* OTG interrupt flags  (W1C, CLR alias only) */
#define PIC32MK_UxOTGIE     0x050u  /* OTG interrupt enable (CLR/SET/INV) */
#define PIC32MK_UxOTGSTAT   0x060u  /* OTG status           (read-only) */
#define PIC32MK_UxOTGCON    0x070u  /* OTG control          (CLR/SET/INV) */
#define PIC32MK_UxPWRC      0x080u  /* Power control        (CLR/SET/INV) */

/* USB core registers */
#define PIC32MK_UxIR        0x200u  /* Interrupt flags  (W1C, CLR alias only) */
#define PIC32MK_UxIE        0x210u  /* Interrupt enable (CLR/SET/INV) */
#define PIC32MK_UxEIR       0x220u  /* Error int flags  (W1C, CLR alias only) */
#define PIC32MK_UxEIE       0x230u  /* Error int enable (CLR/SET/INV) */
#define PIC32MK_UxSTAT      0x240u  /* Status           (read-only) */
#define PIC32MK_UxCON       0x250u  /* Control          (CLR/SET/INV) */
#define PIC32MK_UxADDR      0x260u  /* Device address   (CLR/SET/INV) */
#define PIC32MK_UxBDTP1     0x270u  /* BDT ptr [15:9]   (CLR/SET/INV) */
#define PIC32MK_UxFRML      0x280u  /* Frame# low       (read-only) */
#define PIC32MK_UxFRMH      0x290u  /* Frame# high      (read-only) */
#define PIC32MK_UxTOK       0x2A0u  /* Token register   (CLR/SET/INV) */
#define PIC32MK_UxSOF       0x2B0u  /* SOF threshold    (CLR/SET/INV) */
#define PIC32MK_UxBDTP2     0x2C0u  /* BDT ptr [23:16]  (CLR/SET/INV) */
#define PIC32MK_UxBDTP3     0x2D0u  /* BDT ptr [31:24]  (CLR/SET/INV) */
#define PIC32MK_UxCNFG1     0x2E0u  /* Config 1         (CLR/SET/INV) */

/* Endpoint control registers UxEPn: base + 0x300 + n * 0x10, n = 0..15 */
#define PIC32MK_UxEP_BASE   0x300u
#define PIC32MK_UxEP_STRIDE 0x010u
#define PIC32MK_USB_NEPS    16

/* MMIO size per instance — covers all registers up to UxEP15+CLR/SET/INV */
#define PIC32MK_USB_SFR_SIZE 0x1000u

/* UxCON bits */
#define USB_CON_USBEN   (1u << 0)   /* USB enable (device) / SOF (host) */
#define USB_CON_PPBRST  (1u << 1)   /* Ping-pong buffer reset */
#define USB_CON_RESUME  (1u << 3)   /* Resume signaling */
#define USB_CON_HOSTEN  (1u << 4)   /* Host mode enable */
#define USB_CON_RESET   (1u << 4)   /* Bus reset (host) */

/* UxOTGSTAT bits */
#define USB_OTGSTAT_SESVD  (1u << 3)  /* Session valid (VBUS > V_A_SESS_VLD) */

/* UxOTGIR / UxOTGIE bits (Harmony: USB_OTG_INTERRUPTS enum) */
#define USB_OTG_IR_SESSION_VALID   (1u << 3)  /* VBUS session valid change */
#define USB_OTG_IR_ACTIVITY_DETECT (1u << 4)  /* USB activity detected */

/* UxPWRC bits */
#define USB_PWRC_USBPWR  (1u << 0)  /* USB power enable */
#define USB_PWRC_USUSPND (1u << 1)  /* Suspend */

/* UxIR interrupt bits (same layout for UxIE) */
#define USB_IR_URSTIF   (1u << 0)   /* USB Reset */
#define USB_IR_UERRIF   (1u << 1)   /* Error */
#define USB_IR_SOFIF    (1u << 2)   /* Start-of-frame */
#define USB_IR_TRNIF    (1u << 3)   /* Token done */
#define USB_IR_IDLEIF   (1u << 4)   /* Idle detect */
#define USB_IR_RESUMEIF (1u << 5)   /* Resume */
#define USB_IR_ATTACHIF (1u << 6)   /* Attach (host mode) */
#define USB_IR_STALLIF  (1u << 7)   /* Stall sent */

/* UxSTAT encoding: EP[7:4] | DIR[3] | PPBI[2] */
#define USB_STAT_EP0_OUT_EVEN   0x00u
#define USB_STAT_EP0_IN_EVEN    0x08u
#define USB_STAT_EP1_IN_EVEN    0x18u

/* BDT entry control word bits */
#define BDT_UOWN   (1u << 7)    /* USB owns this buffer (firmware arms it) */

/*
 * Phase 4B EP0 enumeration state machine
 * -----------------------------------------------------------------------
 */

typedef enum {
    EP0_SIM_IDLE,            /* waiting for USBPWR / USBEN */
    EP0_SIM_RESET,           /* fire USB Reset interrupt */
    EP0_SIM_GET_DEV_DESC,    /* inject GET_DESCRIPTOR(Device, 18) */
    EP0_SIM_WAIT_DEV_DESC,   /* waiting for firmware to fill EP0-IN */
    EP0_SIM_SET_ADDRESS,     /* inject SET_ADDRESS(1) */
    EP0_SIM_WAIT_ADDRESS,    /* waiting for status phase */
    EP0_SIM_GET_CFG_DESC,    /* inject GET_DESCRIPTOR(Config, 67) */
    EP0_SIM_WAIT_CFG_DESC,   /* waiting for firmware */
    EP0_SIM_SET_CONFIG,      /* inject SET_CONFIGURATION(1) */
    EP0_SIM_WAIT_CONFIG,     /* waiting for status phase */
    EP0_SIM_DONE,            /* fully enumerated — poll CDC TX BDT */
} EP0SimState;

/*
 * QEMU device model
 * -----------------------------------------------------------------------
 */

#define TYPE_PIC32MK_USB "pic32mk-usb"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKUSBState, PIC32MK_USB)

struct PIC32MKUSBState {
    SysBusDevice parent_obj;

    MemoryRegion sfr_mmio;   /* 0x1000 bytes */

    /* OTG / Power registers */
    uint32_t otgir;          /* UxOTGIR — OTG interrupt flags */
    uint32_t otgie;          /* UxOTGIE */
    uint32_t otgstat;        /* UxOTGSTAT (read-only) */
    uint32_t otgcon;         /* UxOTGCON */
    uint32_t pwrc;           /* UxPWRC */

    /* Core interrupt registers */
    uint32_t uir;            /* UxIR  — USB interrupt flags */
    uint32_t uie;            /* UxIE */
    uint32_t ueir;           /* UxEIR — USB error interrupt flags */
    uint32_t ueie;           /* UxEIE */

    /* Control / Status */
    uint32_t ustat;          /* UxSTAT (read-only, updated by emulator) */
    uint32_t ucon;           /* UxCON */
    uint32_t uaddr;          /* UxADDR */

    /* BDT address pages */
    uint32_t bdtp1;          /* UxBDTP1 — bits [15:9] of BDT base */
    uint32_t bdtp2;          /* UxBDTP2 — bits [23:16] */
    uint32_t bdtp3;          /* UxBDTP3 — bits [31:24] */

    /* Frame counter (read-only; advanced by timer) */
    uint32_t ufrml;
    uint32_t ufrmh;

    /* Token / SOF */
    uint32_t utok;
    uint32_t usof;

    /* Config */
    uint32_t cnfg1;

    /* Endpoint control UxEP0–UxEP15 */
    uint32_t uep[PIC32MK_USB_NEPS];

    /* IRQ line to EVIC */
    qemu_irq irq;

    /* ---- Phase 4B additions ---- */
    QEMUTimer   *usb_timer;     /* drives enumeration + BDT poll */
    CharFrontend chr;           /* virtual serial port for CDC data */
    EP0SimState  ep0_sim;       /* enumeration state machine */
    bool         configured;    /* SET_CONFIGURATION accepted */
    bool         sesvd_edge_latched; /* SESSION_VALID edge latch for IRQ */
    bool         sesvd_acked;        /* firmware has W1C-cleared SESVDIF at least once */
    int          sesvd_retry_count;  /* fallback re-fire counter */

    /* ---- USTAT FIFO (PIC32MK has a 4-deep hardware FIFO) ---- */
    uint32_t stat_fifo[4];      /* circular buffer of pending USTAT values */
    int      stat_fifo_head;    /* next write position */
    int      stat_fifo_tail;    /* next read position */
    int      stat_fifo_count;   /* number of entries in FIFO */
};

#endif /* HW_MIPS_PIC32MK_USB_H */
