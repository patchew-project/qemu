/*
 * QTest for PIC32MK peripheral device models
 *
 * Tests MMIO register access, SET/CLR/INV operations, IRQ assertion,
 * and timer tick generation for the PIC32MK board emulation.
 *
 * Peripherals tested:
 *   - UART  (pic32mk_uart.c) — register reset values, SET/CLR, TX, IRQ
 *   - Timer (pic32mk_timer.c) — ON/OFF, prescaler, period match, IRQ
 *   - EVIC  (pic32mk_evic.c) — IFS/IEC/IPC, interrupt routing
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

/*
 * PIC32MK physical addresses (QTest uses physical, not KSEG1 virtual)
 * SFR base = 0x1F800000
 * -----------------------------------------------------------------------
  */

#define SFR_BASE            0x1F800000u

/* UART1 base = SFR + 0x028000 */
#define UART1_BASE          (SFR_BASE + 0x028000u)
#define UART1_MODE          (UART1_BASE + 0x00u)
#define UART1_MODECLR       (UART1_BASE + 0x04u)
#define UART1_MODESET       (UART1_BASE + 0x08u)
#define UART1_MODEINV       (UART1_BASE + 0x0Cu)
#define UART1_STA           (UART1_BASE + 0x10u)
#define UART1_STACLR        (UART1_BASE + 0x14u)
#define UART1_STASET        (UART1_BASE + 0x18u)
#define UART1_STAINV        (UART1_BASE + 0x1Cu)
#define UART1_TXREG         (UART1_BASE + 0x20u)
#define UART1_RXREG         (UART1_BASE + 0x30u)
#define UART1_BRG            (UART1_BASE + 0x40u)

/* UART bits */
#define UMODE_ON            (1u << 15)
#define USTA_URXDA           (1u << 0)
#define USTA_OERR            (1u << 1)
#define USTA_TRMT            (1u << 8)
#define USTA_UTXBF           (1u << 9)
#define USTA_UTXEN           (1u << 10)
#define USTA_URXEN           (1u << 12)

/* Timer1 base = SFR + 0x020000 */
#define T1_BASE             (SFR_BASE + 0x020000u)
#define T1CON               (T1_BASE + 0x00u)
#define T1CONCLR            (T1_BASE + 0x04u)
#define T1CONSET            (T1_BASE + 0x08u)
#define TMR1                (T1_BASE + 0x10u)
#define PR1                 (T1_BASE + 0x20u)

/* Timer bits */
#define TCON_ON             (1u << 15)

/* EVIC base = SFR + 0x010000 */
#define EVIC_BASE           (SFR_BASE + 0x010000u)
#define EVIC_INTCON         (EVIC_BASE + 0x0000u)
#define EVIC_INTSTAT        (EVIC_BASE + 0x0020u)
#define EVIC_IFS0           (EVIC_BASE + 0x0040u)
#define EVIC_IFS0CLR        (EVIC_BASE + 0x0044u)
#define EVIC_IFS0SET        (EVIC_BASE + 0x0048u)
#define EVIC_IFS1           (EVIC_BASE + 0x0050u)
#define EVIC_IFS1CLR        (EVIC_BASE + 0x0054u)
#define EVIC_IFS1SET        (EVIC_BASE + 0x0058u)
#define EVIC_IEC0           (EVIC_BASE + 0x00C0u)
#define EVIC_IEC0CLR        (EVIC_BASE + 0x00C4u)
#define EVIC_IEC0SET        (EVIC_BASE + 0x00C8u)
#define EVIC_IEC1           (EVIC_BASE + 0x00D0u)
#define EVIC_IEC1CLR        (EVIC_BASE + 0x00D4u)
#define EVIC_IEC1SET        (EVIC_BASE + 0x00D8u)
#define EVIC_IPC0           (EVIC_BASE + 0x0140u)
#define EVIC_IPC1           (EVIC_BASE + 0x0150u)

/* EVIC source bit positions */
#define IRQ_T1_BIT          (1u << 4)       /* Timer1 = source 4, IFS0 bit 4 */
#define IRQ_U1E_BIT         (1u << 6)       /* U1E = source 38 = IFS1 bit 6 */
#define IRQ_U1RX_BIT        (1u << 7)       /* U1RX = source 39 = IFS1 bit 7 */
#define IRQ_U1TX_BIT        (1u << 8)       /* U1TX = source 40 = IFS1 bit 8 */

/*
 * ===================================================================
 * UART1 register tests
 * ===================================================================
  */

static void test_uart_reset_values(void)
{
    /* After machine reset: MODE=0, STA=TRMT (shift register empty), BRG=0 */
    g_assert_cmphex(readl(UART1_MODE), ==, 0x00000000);
    g_assert_cmphex(readl(UART1_STA) & (USTA_TRMT | USTA_UTXBF), ==, USTA_TRMT);
    g_assert_cmphex(readl(UART1_BRG), ==, 0x00000000);
}

static void test_uart_set_clr_inv(void)
{
    /* Write BRG via direct write */
    writel(UART1_BRG, 29);
    g_assert_cmphex(readl(UART1_BRG), ==, 29);

    /* SET operation: set bit 0 of MODE (STSEL) */
    writel(UART1_MODESET, 0x1);
    g_assert_cmphex(readl(UART1_MODE) & 0x1, ==, 0x1);

    /* CLR operation: clear bit 0 */
    writel(UART1_MODECLR, 0x1);
    g_assert_cmphex(readl(UART1_MODE) & 0x1, ==, 0x0);

    /* INV operation: toggle bit 3 (BRGH) */
    uint32_t before = readl(UART1_MODE);
    writel(UART1_MODEINV, 0x8);
    g_assert_cmphex(readl(UART1_MODE), ==, before ^ 0x8);

    /* INV again to toggle back */
    writel(UART1_MODEINV, 0x8);
    g_assert_cmphex(readl(UART1_MODE), ==, before);

    /* Clean up */
    writel(UART1_MODE, 0);
    writel(UART1_BRG, 0);
}

static void test_uart_enable(void)
{
    /* Enable UART: MODESET ON, STASET UTXEN | URXEN */
    writel(UART1_MODESET, UMODE_ON);
    g_assert_cmphex(readl(UART1_MODE) & UMODE_ON, ==, UMODE_ON);

    writel(UART1_STASET, USTA_UTXEN | USTA_URXEN);
    g_assert_cmphex(readl(UART1_STA) & (USTA_UTXEN | USTA_URXEN),
                    ==, USTA_UTXEN | USTA_URXEN);

    /* TRMT should still be 1 (TX buffer always empty in QEMU) */
    g_assert_cmphex(readl(UART1_STA) & USTA_TRMT, ==, USTA_TRMT);

    /* UTXBF should be 0 (transmission is instantaneous) */
    g_assert_cmphex(readl(UART1_STA) & USTA_UTXBF, ==, 0);

    /* Disable UART */
    writel(UART1_MODECLR, UMODE_ON);
    g_assert_cmphex(readl(UART1_MODE) & UMODE_ON, ==, 0);
}

static void test_uart_tx_write(void)
{
    /* Enable UART TX */
    writel(UART1_MODESET, UMODE_ON);
    writel(UART1_STASET, USTA_UTXEN);

    /* Write a character — should not crash, TRMT stays 1 */
    writel(UART1_TXREG, 'A');
    g_assert_cmphex(readl(UART1_STA) & USTA_TRMT, ==, USTA_TRMT);

    /* Write multiple characters */
    writel(UART1_TXREG, 'B');
    writel(UART1_TXREG, 'C');
    g_assert_cmphex(readl(UART1_STA) & USTA_TRMT, ==, USTA_TRMT);

    /* Clean up */
    writel(UART1_MODECLR, UMODE_ON);
}

static void test_uart_rx_not_ready(void)
{
    /* No byte received yet: URXDA should be 0 */
    g_assert_cmphex(readl(UART1_STA) & USTA_URXDA, ==, 0);

    /* Reading RXREG when empty returns 0 */
    g_assert_cmphex(readl(UART1_RXREG), ==, 0);
}

/*
 * ===================================================================
 * EVIC register tests
 * ===================================================================
  */

static void test_evic_reset_values(void)
{
    /* After reset: IFS0=0, IEC0=0, INTCON=0 */
    g_assert_cmphex(readl(EVIC_IFS0), ==, 0x00000000);
    g_assert_cmphex(readl(EVIC_IEC0), ==, 0x00000000);
    g_assert_cmphex(readl(EVIC_INTCON), ==, 0x00000000);
}

static void test_evic_ifs_set_clr(void)
{
    /* SET a flag in IFS0 — e.g. Timer1 (bit 4) */
    writel(EVIC_IFS0SET, IRQ_T1_BIT);
    g_assert_cmphex(readl(EVIC_IFS0) & IRQ_T1_BIT, ==, IRQ_T1_BIT);

    /* CLR the flag */
    writel(EVIC_IFS0CLR, IRQ_T1_BIT);
    g_assert_cmphex(readl(EVIC_IFS0) & IRQ_T1_BIT, ==, 0);
}

static void test_evic_iec_set_clr(void)
{
    /* Enable Timer1 interrupt */
    writel(EVIC_IEC0SET, IRQ_T1_BIT);
    g_assert_cmphex(readl(EVIC_IEC0) & IRQ_T1_BIT, ==, IRQ_T1_BIT);

    /* Disable it */
    writel(EVIC_IEC0CLR, IRQ_T1_BIT);
    g_assert_cmphex(readl(EVIC_IEC0) & IRQ_T1_BIT, ==, 0);
}

static void test_evic_ipc_priority(void)
{
    /* IPC1 holds Timer1 priority (source 4 → IPC1, shift=0, bits[4:2]=IP) */
    /* Set T1 priority to 3: write (3 << 2) = 0x0C into IPC1 bits [4:2] */
    writel(EVIC_IPC1, 0x0C);
    g_assert_cmphex(readl(EVIC_IPC1) & 0x1F, ==, 0x0C);

    /* Clear it back */
    writel(EVIC_IPC1, 0x00);
    g_assert_cmphex(readl(EVIC_IPC1) & 0x1F, ==, 0x00);
}

static void test_evic_uart_ifs1(void)
{
    /* UART1 sources are in IFS1: U1E=bit6, U1RX=bit7, U1TX=bit8 */
    writel(EVIC_IFS1SET, IRQ_U1TX_BIT);
    g_assert_cmphex(readl(EVIC_IFS1) & IRQ_U1TX_BIT, ==, IRQ_U1TX_BIT);

    writel(EVIC_IFS1CLR, IRQ_U1TX_BIT);
    g_assert_cmphex(readl(EVIC_IFS1) & IRQ_U1TX_BIT, ==, 0);

    /* Set all three UART1 flags */
    writel(EVIC_IFS1SET, IRQ_U1E_BIT | IRQ_U1RX_BIT | IRQ_U1TX_BIT);
    g_assert_cmphex(readl(EVIC_IFS1) & (IRQ_U1E_BIT | IRQ_U1RX_BIT | IRQ_U1TX_BIT),
                    ==, IRQ_U1E_BIT | IRQ_U1RX_BIT | IRQ_U1TX_BIT);

    /* Clear all */
    writel(EVIC_IFS1CLR, IRQ_U1E_BIT | IRQ_U1RX_BIT | IRQ_U1TX_BIT);
    g_assert_cmphex(readl(EVIC_IFS1) & (IRQ_U1E_BIT | IRQ_U1RX_BIT | IRQ_U1TX_BIT),
                    ==, 0);
}

static void test_evic_intstat_readonly(void)
{
    /* Write to INTSTAT should be ignored (read-only) */
    uint32_t before = readl(EVIC_INTSTAT);
    writel(EVIC_INTSTAT, 0xDEADBEEF);
    g_assert_cmphex(readl(EVIC_INTSTAT), ==, before);
}

/*
 * ===================================================================
 * Timer1 register tests
 * ===================================================================
  */

static void test_timer_reset_values(void)
{
    /* After reset: T1CON=0 (OFF), TMR1=0, PR1=0xFFFF (16-bit max) */
    g_assert_cmphex(readl(T1CON), ==, 0x00000000);
    g_assert_cmphex(readl(TMR1), ==, 0x00000000);
    g_assert_cmphex(readl(PR1), ==, 0x0000FFFF);
}

static void test_timer_on_off(void)
{
    /* Set period */
    writel(PR1, 14999);

    /* Turn ON via SET register */
    writel(T1CONSET, TCON_ON);
    g_assert_cmphex(readl(T1CON) & TCON_ON, ==, TCON_ON);

    /* Turn OFF via CLR register */
    writel(T1CONCLR, TCON_ON);
    g_assert_cmphex(readl(T1CON) & TCON_ON, ==, 0);

    /* Clean up */
    writel(PR1, 0);
}

static void test_timer_period_register(void)
{
    /* Write period value and read back */
    writel(PR1, 14999);
    g_assert_cmpuint(readl(PR1), ==, 14999);

    /* Different value */
    writel(PR1, 999);
    g_assert_cmpuint(readl(PR1), ==, 999);

    /* Clean up */
    writel(PR1, 0);
}

static void test_timer_prescaler_config(void)
{
    /*
     * Timer1 (Type A) prescaler: bits[5:4] of T1CON
     * 00=1:1, 01=1:8, 10=1:64, 11=1:256
      */
    writel(T1CONSET, (1u << 4));  /* TCKPS=01 → 1:8 */
    g_assert_cmphex(readl(T1CON) & 0x30, ==, 0x10);

    writel(T1CON, 0);  /* reset */
    writel(T1CONSET, (3u << 4));  /* TCKPS=11 → 1:256 */
    g_assert_cmphex(readl(T1CON) & 0x30, ==, 0x30);

    writel(T1CON, 0);
}

static void test_timer_tick_fires_irq(void)
{
    /*
     * Configure Timer1:
     *   Prescaler 1:1 (default), PR1=9 → fires after 10 ticks
     *   Timer clock = 120 MHz internal / 2 (ptimer counts in ns)
     *   ptimer period = (PR1+1) ticks at system clock
     *
     * Timer1 is wired to EVIC source 4 (IFS0 bit 4).
     * Enable IEC0.T1IE and set IPC1.T1IP=1 so the EVIC routes it.
     */

    /* Ensure timer is off and clean */
    writel(T1CON, 0);
    writel(TMR1, 0);

    /* Set period to 9 (fires after 10 timer ticks) */
    writel(PR1, 9);

    /* Enable T1 interrupt in EVIC: IEC0 bit 4, priority 1 in IPC1 */
    writel(EVIC_IFS0CLR, IRQ_T1_BIT);
    writel(EVIC_IEC0SET, IRQ_T1_BIT);
    writel(EVIC_IPC1, (1u << 2));  /* T1IP = 1 */

    /* IFS0 should be clear before starting */
    g_assert_cmphex(readl(EVIC_IFS0) & IRQ_T1_BIT, ==, 0);

    /* Start Timer1: prescaler 1:8 (Type A: TCKPS=01), ON */
    writel(T1CON, (1u << 4) | TCON_ON);

    /*
     * Timer clock = 120 MHz / 8 = 15 MHz → 66.67 ns per tick.
     * With ptimer driven by per-CPU clock, the exact virtual time
     * depends on ptimer frequency setting.  Step enough virtual time
     * for the timer to expire: (PR1+1) * tick_period + margin.
     *
     * ptimer is typically configured at 15 MHz (120/8), so period
     * of 10 ticks = 10 * 66.67ns ≈ 667 ns.  Step 2000 ns to be safe.
     */
    clock_step(2000);

    /* Timer should have fired: IFS0.T1IF should be set */
    g_assert_cmphex(readl(EVIC_IFS0) & IRQ_T1_BIT, ==, IRQ_T1_BIT);

    /* Clear it */
    writel(EVIC_IFS0CLR, IRQ_T1_BIT);
    g_assert_cmphex(readl(EVIC_IFS0) & IRQ_T1_BIT, ==, 0);

    /* Stop timer, clean up */
    writel(T1CON, 0);
    writel(EVIC_IEC0CLR, IRQ_T1_BIT);
    writel(EVIC_IPC1, 0);
}

/*
 * ===================================================================
 * UART1 TX IRQ integration test
 *
 * When UART1 is enabled (ON + UTXEN), the TX IRQ line is asserted.
 * The EVIC should latch IFS1.U1TXIF.
 * ===================================================================
  */

static void test_uart_tx_irq_asserts_ifs(void)
{
    /* Clean initial state */
    writel(EVIC_IFS1CLR, IRQ_U1TX_BIT);
    writel(UART1_MODE, 0);

    /* IFS1.U1TXIF should be clear before UART is enabled */
    g_assert_cmphex(readl(EVIC_IFS1) & IRQ_U1TX_BIT, ==, 0);

    /* Enable UART1: ON + UTXEN */
    writel(UART1_MODESET, UMODE_ON);
    writel(UART1_STASET, USTA_UTXEN);

    /*
     * The UART TX IRQ is level-based — enabling ON+UTXEN asserts it.
     * The EVIC should have set IFS1.U1TXIF.
      */
    g_assert_cmphex(readl(EVIC_IFS1) & IRQ_U1TX_BIT, ==, IRQ_U1TX_BIT);

    /* Clear the flag in IFS1 */
    writel(EVIC_IFS1CLR, IRQ_U1TX_BIT);

    /* Since IRQ line is still high (level-based), EVIC re-asserts IFS */
    g_assert_cmphex(readl(EVIC_IFS1) & IRQ_U1TX_BIT, ==, IRQ_U1TX_BIT);

    /* Disable UART TX — IRQ should deassert */
    writel(UART1_STACLR, USTA_UTXEN);

    /* Now clear IFS1 — it should stay clear since the line is low */
    writel(EVIC_IFS1CLR, IRQ_U1TX_BIT);
    g_assert_cmphex(readl(EVIC_IFS1) & IRQ_U1TX_BIT, ==, 0);

    /* Clean up */
    writel(UART1_MODECLR, UMODE_ON);
}

/*
 * ===================================================================
 * main
 * ===================================================================
  */

int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine pic32mk");

    /* UART1 tests */
    qtest_add_func("/pic32mk/uart/reset-values", test_uart_reset_values);
    qtest_add_func("/pic32mk/uart/set-clr-inv", test_uart_set_clr_inv);
    qtest_add_func("/pic32mk/uart/enable", test_uart_enable);
    qtest_add_func("/pic32mk/uart/tx-write", test_uart_tx_write);
    qtest_add_func("/pic32mk/uart/rx-not-ready", test_uart_rx_not_ready);

    /* EVIC tests */
    qtest_add_func("/pic32mk/evic/reset-values", test_evic_reset_values);
    qtest_add_func("/pic32mk/evic/ifs-set-clr", test_evic_ifs_set_clr);
    qtest_add_func("/pic32mk/evic/iec-set-clr", test_evic_iec_set_clr);
    qtest_add_func("/pic32mk/evic/ipc-priority", test_evic_ipc_priority);
    qtest_add_func("/pic32mk/evic/uart-ifs1", test_evic_uart_ifs1);
    qtest_add_func("/pic32mk/evic/intstat-readonly", test_evic_intstat_readonly);

    /* Timer1 tests */
    qtest_add_func("/pic32mk/timer/reset-values", test_timer_reset_values);
    qtest_add_func("/pic32mk/timer/on-off", test_timer_on_off);
    qtest_add_func("/pic32mk/timer/period-register", test_timer_period_register);
    qtest_add_func("/pic32mk/timer/prescaler-config", test_timer_prescaler_config);
    qtest_add_func("/pic32mk/timer/tick-fires-irq", test_timer_tick_fires_irq);

    /* Integration: UART TX IRQ → EVIC */
    qtest_add_func("/pic32mk/integration/uart-tx-irq", test_uart_tx_irq_asserts_ifs);

    r = g_test_run();

    qtest_end();

    return r;
}
