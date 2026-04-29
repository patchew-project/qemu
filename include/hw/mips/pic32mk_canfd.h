/*
 * Microchip PIC32MK CAN FD controller — device interface
 * Based on PIC32MK GPK/MCM with CAN FD Family Datasheet (DS60001519E)
 * and Microchip CAN FD Controller Reference Manual (DS60001507).
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MIPS_PIC32MK_CANFD_H
#define HW_MIPS_PIC32MK_CANFD_H

#include "hw/core/sysbus.h"
#include "net/can_emu.h"
#include "qom/object.h"

#define TYPE_PIC32MK_CANFD  "pic32mk-canfd"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKCANFDState, PIC32MK_CANFD)

/*
 * CAN FD SFR register offsets (from instance base).
 *
 * Layout verified against XC32 device header p32mk1024mcm100.h.
 * Each logical register occupies 0x10 bytes:
 *   +0x0 = base, +0x4 = SET, +0x8 = CLR, +0xC = INV
 * -----------------------------------------------------------------------
 */

#define CANFD_CiCON         0x000u  /* Control */
#define CANFD_CiNBTCFG      0x010u  /* Nominal bit-time config */
#define CANFD_CiDBTCFG      0x020u  /* Data bit-time config */
#define CANFD_CiTDC         0x030u  /* Transmitter delay compensation */
#define CANFD_CiTBC         0x040u  /* Time base counter */
#define CANFD_CiTSCON       0x050u  /* Timestamp control */
#define CANFD_CiVEC         0x060u  /* Interrupt flag code */
#define CANFD_CiINT         0x070u  /* Interrupt aggregator */
#define CANFD_CiRXIF        0x080u  /* RX interrupt flags per FIFO */
#define CANFD_CiTXIF        0x090u  /* TX interrupt flags per FIFO */
#define CANFD_CiRXOVIF      0x0A0u  /* RX overflow flags per FIFO */
#define CANFD_CiTXATIF      0x0B0u  /* TX attempts interrupt flags */
#define CANFD_CiTXREQ       0x0C0u  /* TX request bits per FIFO */
#define CANFD_CiTREC        0x0D0u  /* TX/RX error counters */
#define CANFD_CiBDIAG0      0x0E0u  /* Bus diagnostic 0 */
#define CANFD_CiBDIAG1      0x0F0u  /* Bus diagnostic 1 */
#define CANFD_CiTEFCON      0x100u  /* TX Event FIFO control */
#define CANFD_CiTEFSTA      0x110u  /* TX Event FIFO status */
#define CANFD_CiTEFUA       0x120u  /* TX Event FIFO user address */
#define CANFD_CiFIFOBA      0x130u  /* Message RAM base address (Phase 3C) */
#define CANFD_CiTXQCON      0x140u  /* TX Queue control */
#define CANFD_CiTXQSTA      0x150u  /* TX Queue status */
#define CANFD_CiTXQUA       0x160u  /* TX Queue user address */
/* FIFO n (n=1..31): stride 0x30 (3 registers × 0x10 each) */
#define CANFD_CiFIFOCON(n)  (0x170u + ((n) - 1u) * 0x30u)
#define CANFD_CiFIFOSTA(n)  (0x180u + ((n) - 1u) * 0x30u)
#define CANFD_CiFIFOUA(n)   (0x190u + ((n) - 1u) * 0x30u)
/* Filter control: stride 0x10 per register, 8 registers */
#define CANFD_CiFLTCON(r)   (0x740u + (r) * 0x10u)
/* Filter object/mask pairs: stride 0x20 per pair */
#define CANFD_CiFLTOBJ(n)   (0x7C0u + (n) * 0x20u)
#define CANFD_CiMASK(n)     (0x7D0u + (n) * 0x20u)

/*
 * CiCON bit fields
 * -----------------------------------------------------------------------
 */
/*
 * CiCON field positions verified against DS60001519E / MCP251xFD IP core:
 *   reset value 0x04980760 → REQOP=4 (Config), OPMOD=4 (Config)
 */
#define CANFD_CON_REQOP_SHIFT   24u
#define CANFD_CON_REQOP_MASK    (0x7u << CANFD_CON_REQOP_SHIFT)
#define CANFD_CON_OPMOD_SHIFT   21u
#define CANFD_CON_OPMOD_MASK    (0x7u << CANFD_CON_OPMOD_SHIFT)
#define CANFD_CON_TXQEN         (1u << 20u)
#define CANFD_CON_STEF          (1u << 19u)
#define CANFD_CON_ABAT          (1u << 27u)
#define CANFD_CON_RESET         0x04980760u  /* Config mode on reset */

/* OPMOD values */
#define CANFD_OPMOD_NORMAL      0u
#define CANFD_OPMOD_RESTRICTED  1u
#define CANFD_OPMOD_LISTEN      2u
#define CANFD_OPMOD_CONFIG      4u
#define CANFD_OPMOD_EXT_LOOP    5u
#define CANFD_OPMOD_INT_LOOP    7u

/*
 * CiINT bit fields — verified against XC32 p32mk1024mcm100.h
 * Lower 16 bits = status flags, upper 16 bits = enable bits.
 * -----------------------------------------------------------------------
 */
#define CANFD_INT_TXIF      (1u << 0u)   /* TX FIFO interrupt flag */
#define CANFD_INT_RXIF      (1u << 1u)   /* RX FIFO interrupt flag */
#define CANFD_INT_MODIF     (1u << 3u)   /* Mode change flag */
#define CANFD_INT_TEFIF     (1u << 4u)   /* TEF interrupt flag */
#define CANFD_INT_TXATIF    (1u << 10u)  /* TX attempts flag */
#define CANFD_INT_RXOVIF    (1u << 11u)  /* RX overflow flag */
#define CANFD_INT_TXIE      (1u << 16u)  /* TX IRQ enable */
#define CANFD_INT_RXIE      (1u << 17u)  /* RX IRQ enable */
#define CANFD_INT_MODIE     (1u << 19u)  /* Mode change IRQ enable */
#define CANFD_INT_RXOVIE    (1u << 27u)  /* RX overflow IRQ enable */
#define CANFD_INT_SERRIE    (1u << 28u)  /* System error IRQ enable */
#define CANFD_INT_CERRIE    (1u << 29u)  /* CAN bus error IRQ enable */
#define CANFD_INT_IVMIE     (1u << 31u)  /* Invalid message IRQ enable */

/*
 * CiFIFOCONn / CiTXQCON bit fields
 * Bit positions verified against XC32 p32mk1024mcm100.h device header.
 * -----------------------------------------------------------------------
 */
#define CANFD_FIFO_PLSIZE_SHIFT 29u
#define CANFD_FIFO_PLSIZE_MASK  (0x7u << CANFD_FIFO_PLSIZE_SHIFT)
#define CANFD_FIFO_FSIZE_SHIFT  24u
#define CANFD_FIFO_FSIZE_MASK   (0x1Fu << CANFD_FIFO_FSIZE_SHIFT)
#define CANFD_FIFO_TFNRFNIE     (1u << 0u)   /* RX: FIFO Not Empty interrupt enable */
#define CANFD_FIFO_TXEN         (1u << 7u)   /* TX enable (1=TX FIFO, 0=RX FIFO) */
#define CANFD_FIFO_UINC         (1u << 8u)   /* User increment (pulse bit) */
#define CANFD_FIFO_TXREQ        (1u << 9u)   /* TX request (pulse bit) */
#define CANFD_FIFO_FRESET       (1u << 10u)  /* FIFO reset (pulse bit) */

/* CiFIFOSTAn bit fields — verified against XC32 device header */
#define CANFD_FIFOSTA_TFNRFNIF  (1u << 0u)  /* TX not full / RX not empty */
#define CANFD_FIFOSTA_TXATIF    (1u << 4u)  /* TX attempts exhausted */
#define CANFD_TXQSTA_TXQNIF     (1u << 0u)  /* CiTXQSTA: TX Queue not full */

/*
 * Device state
 * -----------------------------------------------------------------------
 */

struct PIC32MKCANFDState {
    SysBusDevice parent_obj;

    /* Two MemoryRegions per instance */
    MemoryRegion sfr_mmio;   /* SFR registers: PIC32MK_CAN_SFR_SIZE bytes */
    MemoryRegion msg_ram;    /* Message RAM: PIC32MK_CAN_MSGRAM_SIZE bytes */

    /* Key SFRs (reset values per DS60001507 §3) */
    uint32_t con;            /* CiCON  — reset 0x04980760 */
    uint32_t nbtcfg;         /* CiNBTCFG */
    uint32_t dbtcfg;         /* CiDBTCFG */
    uint32_t tdc;            /* CiTDC */
    uint32_t tbc;            /* CiTBC (free-running) */
    uint32_t tscon;          /* CiTSCON */
    uint32_t vec;            /* CiVEC */
    uint32_t cint;           /* CiINT */
    uint32_t rxif;           /* CiRXIF */
    uint32_t txif;           /* CiTXIF */
    uint32_t rxovif;         /* CiRXOVIF */
    uint32_t txreq;          /* CiTXREQ */
    uint32_t trec;           /* CiTREC */

    /* TXQ */
    uint32_t txqcon;
    uint32_t txqsta;
    uint32_t txqua;
    uint8_t  txq_head;       /* oldest pending frame (TX processes from here) */
    uint8_t  txq_tail;       /* next empty slot firmware will write to */
    uint8_t  txq_count;      /* number of frames queued */

    /* TEF */
    uint32_t tefcon;
    uint32_t tefsta;
    uint32_t tefua;

    /* Per-FIFO state (index 1–31; [0] unused) */
    uint32_t fifocon[32];
    uint32_t fifosta[32];
    uint32_t fifoua[32];
    uint8_t  fifo_head[32];
    uint8_t  fifo_tail[32];
    uint8_t  fifo_count[32];

    /* Acceptance filters */
    uint32_t fltcon[8];      /* 4 filters per reg × 8 = 32 filters */
    uint32_t fltobj[32];
    uint32_t mask[32];

    /* Message RAM backing store */
    uint8_t  *msg_ram_buf;
    uint32_t  msg_ram_phys;  /* physical base (injected via property) */

    /* Instance index (0–3 for CAN1–4) */
    uint32_t  instance_id;

    /* Single IRQ line to EVIC */
    qemu_irq irq;

    /* SocketCAN virtual bus (Phase 3B) */
    CanBusClientState bus_client;
    CanBusState      *canbus;   /* linked can-bus object, or NULL */

    /*
     * Bus-side software ring buffer — decouples SocketCAN delivery timing
     * from the guest CPU.  Frames land here when the target RX FIFO is full;
     * canfd_bus_buf_drain() moves them into the FIFO after each UINC.
      */
    uint32_t bus_buf_id[64];
    bool     bus_buf_xtd[64];
    bool     bus_buf_fdf[64];
    uint8_t  bus_buf_dlc[64];
    uint8_t  bus_buf_data[64][64];
    int      bus_buf_len[64];
    int      bus_buf_dest[64];
    int      bus_buf_head;
    int      bus_buf_tail;
    int      bus_buf_count;
};

#endif /* HW_MIPS_PIC32MK_CANFD_H */
