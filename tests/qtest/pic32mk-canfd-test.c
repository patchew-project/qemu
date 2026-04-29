/*
 * QTest for PIC32MK CAN FD controller emulation (pic32mk_canfd.c)
 *
 * Exercises:
 *   - Reset state of CiCON, CiINT, CiTXQSTA
 *   - Mode transitions: Config → Normal, Config → Internal Loopback
 *   - CiINT.MODIF assertion and firmware clear via read-modify-write
 *   - TXQ / FIFO configuration and UA register computation
 *   - Internal loopback: TX frame → acceptance filter → RX FIFO
 *   - UINC pointer protocol (TX and RX sides)
 *   - RX overflow detection via CiRXOVIF / CiINT.RXOVIF
 *   - IRQ flag propagation (CiRXIF, CiTXIF, CiINT.RXIF, CiINT.TXIF)
 *
 * All addresses are physical (QTest bypasses the MIPS KSEG1 mapping).
 * CAN FD SFR registers use 0x10-byte blocks: base/+4(SET)/+8(CLR)/+C(INV).
 * Tests use plain writes to the base address (sub=0).
 *
 * Register offsets verified against XC32 p32mk1024mcm100.h device header.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

/*
 * Address map
 * Physical SFR base = 0x1F800000
 * CAN1 SFR  = SFR + 0x080000 = 0x1F880000
 * CAN1 MSGRAM = 0x1F900000  (placeholder, Phase 3C will use CiFIFOBA)
 * -----------------------------------------------------------------------
  */

#define SFR_BASE            0x1F800000u
#define CAN1_BASE           (SFR_BASE + 0x080000u)
#define CAN1_MSGRAM_BASE    0x1F900000u

/* CiXxx register physical addresses for CAN1 (base of each 0x10-byte block) */
#define CiCON               (CAN1_BASE + 0x000u)
#define CiNBTCFG            (CAN1_BASE + 0x010u)
#define CiDBTCFG            (CAN1_BASE + 0x020u)
#define CiINT               (CAN1_BASE + 0x070u)
#define CiRXIF              (CAN1_BASE + 0x080u)
#define CiTXIF              (CAN1_BASE + 0x090u)
#define CiRXOVIF            (CAN1_BASE + 0x0A0u)
#define CiTXREQ             (CAN1_BASE + 0x0C0u)
#define CiTEFCON            (CAN1_BASE + 0x100u)
#define CiFIFOBA            (CAN1_BASE + 0x130u)
#define CiTXQCON            (CAN1_BASE + 0x140u)
#define CiTXQSTA            (CAN1_BASE + 0x150u)
#define CiTXQUA             (CAN1_BASE + 0x160u)

/* FIFO1 registers: base = 0x170 + (1-1)*0x30 = 0x170 */
#define CiFIFOCON1          (CAN1_BASE + 0x170u)
#define CiFIFOSTA1          (CAN1_BASE + 0x180u)
#define CiFIFOUA1           (CAN1_BASE + 0x190u)

/* FIFO2 registers: base = 0x170 + (2-1)*0x30 = 0x1A0 */
#define CiFIFOCON2          (CAN1_BASE + 0x1A0u)
#define CiFIFOSTA2          (CAN1_BASE + 0x1B0u)
#define CiFIFOUA2           (CAN1_BASE + 0x1C0u)

/* Filter control: stride 0x10 per reg (r=0..7) */
#define CiFLTCON0           (CAN1_BASE + 0x740u)

/* Filter object/mask: stride 0x20 per pair */
#define CiFLTOBJ0           (CAN1_BASE + 0x7C0u)
#define CiMASK0             (CAN1_BASE + 0x7D0u)

/* SET/CLR aliases for key registers (used in some tests) */
#define CiCON_SET           (CAN1_BASE + 0x004u)
#define CiCON_CLR           (CAN1_BASE + 0x008u)

/*
 * CiCON field masks — verified against reset value 0x04980760
 * -----------------------------------------------------------------------
  */
#define CON_REQOP_SHIFT     24u
#define CON_REQOP_MASK      (0x7u << CON_REQOP_SHIFT)
#define CON_OPMOD_SHIFT     21u
#define CON_OPMOD_MASK      (0x7u << CON_OPMOD_SHIFT)
#define CON_TXQEN           (1u << 20u)
#define CON_RESET_VAL       0x04980760u

#define OPMOD_NORMAL        0u
#define OPMOD_CONFIG        4u
#define OPMOD_INT_LOOP      7u

/* CiINT bits — verified against XC32 device header */
#define INT_TXIF            (1u << 0u)
#define INT_RXIF            (1u << 1u)
#define INT_MODIF           (1u << 3u)   /* bit 3 on real hardware */
#define INT_RXOVIF          (1u << 11u)
#define INT_TXIE            (1u << 16u)  /* bit 16 on real hardware */
#define INT_RXIE            (1u << 17u)  /* bit 17 on real hardware */
#define INT_MODIE           (1u << 19u)  /* bit 19 on real hardware */
#define INT_RXOVIE          (1u << 27u)

/* CiFIFOCONn / CiTXQCON bits — verified against XC32 device header */
#define FIFO_PLSIZE_SHIFT   29u
#define FIFO_FSIZE_SHIFT    24u
#define FIFO_TXEN           (1u << 7u)   /* bit 7 on real hardware */
#define FIFO_UINC           (1u << 8u)   /* bit 8 on real hardware */
#define FIFO_TXREQ          (1u << 9u)   /* bit 9 on real hardware */
#define FIFO_FRESET         (1u << 10u)  /* bit 10 on real hardware */

/* CiFIFOSTAn bits */
#define FIFOSTA_TFNRFNIF    (1u << 0u)   /* TX not full / RX not empty */
#define FIFOSTA_TXATIF      (1u << 4u)

/* CiTXQSTA bits */
#define TXQSTA_TXQNIF       (1u << 0u)   /* bit 0 on real hardware */

/*
 * Helpers
 * -----------------------------------------------------------------------
  */

/* Enter the requested mode; asserts OPMOD matches after write */
static void set_mode(uint32_t opmod_val)
{
    uint32_t con = readl(CiCON);
    con = (con & ~CON_REQOP_MASK) | (opmod_val << CON_REQOP_SHIFT);
    writel(CiCON, con);
    uint32_t result = readl(CiCON);
    g_assert_cmphex((result & CON_OPMOD_MASK) >> CON_OPMOD_SHIFT,
                    ==, opmod_val);
}

/* Clear specific bits in CiINT via read-modify-write */
static void clear_cint_bits(uint32_t mask)
{
    writel(CiINT, readl(CiINT) & ~mask);
}

/*
 * T1: Reset state
 * -----------------------------------------------------------------------
  */

static void test_reset_state(void)
{
    /* CiCON must come up in Config mode */
    uint32_t con = readl(CiCON);
    g_assert_cmphex(con, ==, CON_RESET_VAL);

    uint8_t opmod = (con & CON_OPMOD_MASK) >> CON_OPMOD_SHIFT;
    g_assert_cmpuint(opmod, ==, OPMOD_CONFIG);

    /* CiINT must be clear */
    g_assert_cmphex(readl(CiINT), ==, 0u);

    /* CiTXQSTA: TXQNIF (bit 0) should be set (slot available) */
    g_assert_true((readl(CiTXQSTA) & TXQSTA_TXQNIF) != 0u);
}

/*
 * T2: Mode transition Config → Normal, MODIF assertion, and clear
 * -----------------------------------------------------------------------
  */

static void test_mode_transition(void)
{
    /* Start in Config mode (reset state) */
    g_assert_cmpuint((readl(CiCON) & CON_OPMOD_MASK) >> CON_OPMOD_SHIFT,
                     ==, OPMOD_CONFIG);

    /* Request Normal mode */
    set_mode(OPMOD_NORMAL);

    /* MODIF (bit 3) must be set in CiINT */
    g_assert_true((readl(CiINT) & INT_MODIF) != 0u);

    /* Clear MODIF via read-modify-write */
    clear_cint_bits(INT_MODIF);
    g_assert_false((readl(CiINT) & INT_MODIF) != 0u);

    /* Transition back to Config mode for subsequent tests */
    set_mode(OPMOD_CONFIG);
    clear_cint_bits(INT_MODIF);
}

/*
 * T3: TXQ configuration and UA register computation
 * -----------------------------------------------------------------------
  */

static void test_txq_configure(void)
{
    /* Must be in Config mode */
    set_mode(OPMOD_CONFIG);

    /*
     * Configure TXQ: PLSIZE=0 (8-byte payload), FSIZE=0 (1 entry).
     * Enable TXQ in CiCON via read-modify-write.
     */
    uint32_t txqcon = (0u << FIFO_PLSIZE_SHIFT) | (0u << FIFO_FSIZE_SHIFT);
    writel(CiTXQCON, txqcon);
    writel(CiCON, readl(CiCON) | CON_TXQEN);

    /* UA must point to the message RAM base for CAN1 */
    uint32_t ua = readl(CiTXQUA);
    g_assert_cmphex(ua, ==, CAN1_MSGRAM_BASE);
}

/*
 * T4: Internal loopback — TX a frame via TXQ, receive it in FIFO1
 * -----------------------------------------------------------------------
  */

static void test_loopback_tx_rx(void)
{
    /* --- Configure in Config mode --- */
    set_mode(OPMOD_CONFIG);

    /* TXQ: 8-byte payload, depth 2 (FSIZE=1) */
    uint32_t txqcon = (0u << FIFO_PLSIZE_SHIFT) | (1u << FIFO_FSIZE_SHIFT);
    writel(CiTXQCON, txqcon);
    writel(CiCON, readl(CiCON) | CON_TXQEN);

    /* FIFO1 as RX: TXEN=0, PLSIZE=0 (8B), FSIZE=1 (2 entries), FRESET */
    uint32_t fifocon1 = (0u << FIFO_PLSIZE_SHIFT) | (1u << FIFO_FSIZE_SHIFT)
                      | FIFO_FRESET;
    writel(CiFIFOCON1, fifocon1);

    /*
     * Filter 0: match SID 0x123 exactly.
     * CiFLTOBJ0: standard ID in bits [10:0] (XTD=0).
     * CiMASK0: 0x1FFFFFFF forces all SID bits to match.
     * CiFLTCON0 byte 0: FLTEN=1 (bit7), FIFO=1 (bits [4:0]) → 0x81.
     */
    writel(CiFLTOBJ0, 0x00000123u);  /* SID=0x123, XTD=0 */
    writel(CiMASK0,   0x1FFFFFFFu);  /* exact match */
    /* CiFLTCON0: byte 0 = 0x81 (FLTEN=1, FIFO destination=1) */
    writel(CiFLTCON0, 0x00000081u);

    /* Enable RXIE and TXIE */
    writel(CiINT, readl(CiINT) | INT_RXIE | INT_TXIE);

    /* Enter internal loopback mode */
    set_mode(OPMOD_INT_LOOP);
    clear_cint_bits(INT_MODIF);

    /* --- Write TX object at TXQ UA --- */
    uint32_t txqua = readl(CiTXQUA);
    g_assert_cmphex(txqua, !=, 0u);

    /*
     * T0: Standard ID 0x123 in bits [28:18] (shifted left by 18).
     * T1: DLC=2 (2 bytes), classic CAN (FDF=0).
     * Payload: 0xDEAD in first two bytes (little-endian word).
     */
    uint32_t t0 = 0x123u << 18u;
    uint32_t t1 = 2u;
    uint32_t payload = 0x0000DEADu;

    writel(txqua + 0u, t0);
    writel(txqua + 4u, t1);
    writel(txqua + 8u, payload);

    /* UINC — advance write pointer (hardware clears the bit) */
    writel(CiTXQCON, readl(CiTXQCON) | FIFO_UINC);
    g_assert_false((readl(CiTXQCON) & FIFO_UINC) != 0u);

    /* TXREQ — trigger transmission */
    writel(CiTXQCON, readl(CiTXQCON) | FIFO_TXREQ);

    /* --- Verify RX delivery --- */

    /* CiRXIF bit 1 (FIFO1) must be set */
    g_assert_true((readl(CiRXIF) & (1u << 1u)) != 0u);

    /* CiINT.RXIF must be set */
    g_assert_true((readl(CiINT) & INT_RXIF) != 0u);

    /* CiINT.TXIF must be set */
    g_assert_true((readl(CiINT) & INT_TXIF) != 0u);

    /* Read RX object from FIFO1 UA */
    uint32_t fifoua1 = readl(CiFIFOUA1);
    g_assert_cmphex(fifoua1, !=, 0u);

    uint32_t r0    = readl(fifoua1 + 0u);
    uint32_t r1    = readl(fifoua1 + 4u);
    uint32_t rdata = readl(fifoua1 + 8u);

    /* R0: standard ID in [28:18] */
    uint32_t rx_sid = (r0 >> 18u) & 0x7FFu;
    g_assert_cmphex(rx_sid, ==, 0x123u);

    /* R1: DLC=2 */
    g_assert_cmpuint(r1 & 0xFu, ==, 2u);

    /* FILHIT in R1[15:11] should be 1 (filter 0 maps to FIFO 1) */
    uint32_t filhit = (r1 >> 11u) & 0x1Fu;
    g_assert_cmpuint(filhit, ==, 1u);

    /* Payload: first word */
    g_assert_cmphex(rdata & 0x0000FFFFu, ==, 0x0000DEADu);

    /* UINC on FIFO1 — firmware done reading, advance read pointer */
    writel(CiFIFOCON1, readl(CiFIFOCON1) | FIFO_UINC);
    g_assert_false((readl(CiFIFOCON1) & FIFO_UINC) != 0u);

    /* CiRXIF bit 1 must now be clear (FIFO empty) */
    g_assert_false((readl(CiRXIF) & (1u << 1u)) != 0u);

    /* CiINT.RXIF must be clear */
    g_assert_false((readl(CiINT) & INT_RXIF) != 0u);

    /* --- Cleanup --- */
    clear_cint_bits(INT_RXIE | INT_TXIE | INT_TXIF | INT_RXIF | INT_MODIF);
    set_mode(OPMOD_CONFIG);
    clear_cint_bits(INT_MODIF);
}

/*
 * T5: RX overflow — FIFO depth 1, deliver 2 frames, verify RXOVIF
 * -----------------------------------------------------------------------
  */

static void test_rx_overflow(void)
{
    set_mode(OPMOD_CONFIG);

    /* TXQ: 8-byte payload, depth 4 (FSIZE=3) */
    uint32_t txqcon = (0u << FIFO_PLSIZE_SHIFT) | (3u << FIFO_FSIZE_SHIFT);
    writel(CiTXQCON, txqcon);
    writel(CiCON, readl(CiCON) | CON_TXQEN);

    /* FIFO2 as RX, depth 1 (FSIZE=0) — will overflow on 2nd frame */
    writel(CiFIFOCON2, (0u << FIFO_PLSIZE_SHIFT) | (0u << FIFO_FSIZE_SHIFT)
                     | FIFO_FRESET);

    /*
     * Filter 1 (byte 1 in CiFLTCON0): match all IDs → FIFO2.
     * byte0 = 0x00 (filter 0 disabled), byte1 = 0x82 (FLTEN=1, FIFO=2).
     */
    writel(CiFLTOBJ0, 0x00000000u);
    writel(CiMASK0,   0x00000000u);  /* no bits required to match */
    writel(CiFLTCON0, 0x00008200u);

    /* Enable RXOVIE */
    writel(CiINT, readl(CiINT) | INT_RXOVIE | INT_RXIE);

    set_mode(OPMOD_INT_LOOP);
    clear_cint_bits(INT_MODIF);

    /* Send two frames via TXQ */
    for (int i = 0; i < 2; i++) {
        uint32_t ua = readl(CiTXQUA);
        writel(ua + 0u, 0u);           /* T0: SID=0, XTD=0 */
        writel(ua + 4u, 1u);           /* T1: DLC=1 */
        writel(ua + 8u, (uint32_t)i);  /* payload */
        writel(CiTXQCON, readl(CiTXQCON) | FIFO_UINC);
        writel(CiTXQCON, readl(CiTXQCON) | FIFO_TXREQ);
    }

    /* CiRXOVIF bit 2 (FIFO2) must be set */
    g_assert_true((readl(CiRXOVIF) & (1u << 2u)) != 0u);

    /* CiINT.RXOVIF must be set */
    g_assert_true((readl(CiINT) & INT_RXOVIF) != 0u);

    /* Cleanup */
    writel(CiRXOVIF, 0u);
    clear_cint_bits(INT_RXOVIE | INT_RXIE | INT_RXOVIF | INT_RXIF | INT_MODIF);
    set_mode(OPMOD_CONFIG);
    clear_cint_bits(INT_MODIF);
}

/*
 * T6: CiCON TXQEN bit set/clear via read-modify-write
 * -----------------------------------------------------------------------
  */

static void test_con_rmw(void)
{
    set_mode(OPMOD_CONFIG);

    /* Clear TXQEN */
    writel(CiCON, readl(CiCON) & ~CON_TXQEN);
    g_assert_false((readl(CiCON) & CON_TXQEN) != 0u);

    /* Set TXQEN */
    writel(CiCON, readl(CiCON) | CON_TXQEN);
    g_assert_true((readl(CiCON) & CON_TXQEN) != 0u);
}

/*
 * Main
 * -----------------------------------------------------------------------
  */

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_start("-machine pic32mk");

    qtest_add_func("/pic32mk-canfd/reset_state",      test_reset_state);
    qtest_add_func("/pic32mk-canfd/mode_transition",  test_mode_transition);
    qtest_add_func("/pic32mk-canfd/txq_configure",    test_txq_configure);
    qtest_add_func("/pic32mk-canfd/loopback_tx_rx",   test_loopback_tx_rx);
    qtest_add_func("/pic32mk-canfd/rx_overflow",      test_rx_overflow);
    qtest_add_func("/pic32mk-canfd/con_rmw",          test_con_rmw);

    int r = g_test_run();
    qtest_end();
    return r;
}
