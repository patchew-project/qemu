/*
 * PIC32MK GPK/MCM with CAN FD — shared constants
 * Datasheet: DS60001519E
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MIPS_PIC32MK_H
#define HW_MIPS_PIC32MK_H

/*
 * Physical memory map (§4, pp. 70-73)
 * -----------------------------------------------------------------------
 */

#define PIC32MK_RAM_BASE        0x00000000u   /* 256 KB SRAM */
#define PIC32MK_RAM_SIZE        (256 * 1024)

#define PIC32MK_PFLASH_BASE     0x1D000000u   /* 1 MB Program Flash */
#define PIC32MK_PFLASH_SIZE     (1 * 1024 * 1024)

/*
 * Boot vector ROM — fills the gap between the MIPS reset vector (0x1FC00000)
 * and Boot Flash 1 (0x1FC40000). Contains a trampoline that jumps to BFlash1.
 */
#define PIC32MK_BOOTVEC_BASE    0x1FC00000u   /* physical reset vector */
#define PIC32MK_BOOTVEC_SIZE    0x00040000u   /* 256 KB gap to BFlash1 */

#define PIC32MK_BFLASH1_BASE    0x1FC40000u   /* Boot Flash 1 — enlarged for firmware */
#define PIC32MK_BFLASH1_SIZE    (256 * 1024)

#define PIC32MK_BFLASH2_BASE    0x1FC60000u   /* Boot Flash 2 ~20 KB */
#define PIC32MK_BFLASH2_SIZE    (20 * 1024)

#define PIC32MK_SFR_BASE        0x1F800000u   /* SFR window, 1 MB */
#define PIC32MK_SFR_SIZE        (1 * 1024 * 1024)

/* Reset vector (KSEG1 uncached alias of 0x1FC00000) */
#define PIC32MK_RESET_VECTOR    0xBFC00000u

/*
 * SFR sub-block bases (offsets within the 1 MB SFR window, Table 4-2)
 * All addresses below are KSEG1 virtual (0xBF800000 + offset).
 * -----------------------------------------------------------------------
 */

/* CFG / PMD block (0xBF800000)                                           */
#define PIC32MK_CFG_OFFSET      0x000000u
#define PIC32MK_CFG_SIZE        0x000900u   /* CFGCON..CFGCON2+INV + CHECON@0x800 */

/* CRU — Clock Reference Unit (0xBF801200)                                 */
#define PIC32MK_CRU_OFFSET      0x001200u
#define PIC32MK_CRU_SIZE        0x0001A0u   /* OSCCON..CLKSTAT+INV inclusive */

/* PPS (Peripheral Pin Select) input/output registers (0xBF801400–0xBF8017FF) */
#define PIC32MK_PPS_OFFSET      0x001400u
#define PIC32MK_PPS_SIZE        0x000400u

/* WDT register block (0xBF800C00) */
#define PIC32MK_WDT_OFFSET      0x000C00u
#define PIC32MK_WDT_SIZE        0x000040u   /* WDTCON + SET/CLR/INV + padding */

/* EVIC                                         0xBF810000 */
#define PIC32MK_EVIC_OFFSET     0x010000u

/* DMA (shares page with EVIC)                  0xBF811000 */
#define PIC32MK_DMA_OFFSET      0x011000u

/* Timers / IC / OC / I2C1-2 / SPI1-2 / UART1-2 / PWM / QEI / CMP / CDAC1 */
#define PIC32MK_PER1_OFFSET     0x020000u   /* 0xBF820000, size 0xD000 */

/* I2C3-4 / SPI3-6 / UART3-6 / CDAC2-3         0xBF840000 */
#define PIC32MK_PER2_OFFSET     0x040000u

/* GPIO PORTA-PORTG                             0xBF860000 */
#define PIC32MK_GPIO_OFFSET     0x060000u

/* CAN1-4 / ADC                                 0xBF880000 */
#define PIC32MK_CAN_OFFSET      0x080000u

/*
 * CAN1–4 SFR offsets from SFR base
 * Verified against DS60001519E / p32mk1024mcm100.h:
 *   CFD1CON @ 0xBF880000, CFD2CON @ 0xBF881000,
 *   CFD3CON @ 0xBF884000, CFD4CON @ 0xBF885000
 */
#define PIC32MK_CAN1_OFFSET     0x080000u   /* physical 0x1F880000 */
#define PIC32MK_CAN2_OFFSET     0x081000u   /* physical 0x1F881000 */
#define PIC32MK_CAN3_OFFSET     0x084000u   /* physical 0x1F884000 */
#define PIC32MK_CAN4_OFFSET     0x085000u   /* physical 0x1F885000 */
#define PIC32MK_CAN_SFR_SIZE    0x1000u     /* 4 KB SFR block per instance */

/*
 * Message RAM physical base per instance.
 * Allocated just above the SFR window, beyond the ADC / USB blocks.
 * ⚠  Verify exact addresses from DS60001519E §4 before final integration.
 * Each instance gets 76 KB (75 KB data + alignment).
 */
#define PIC32MK_CAN1_MSGRAM_BASE 0x1F900000u
#define PIC32MK_CAN2_MSGRAM_BASE 0x1F913000u
#define PIC32MK_CAN3_MSGRAM_BASE 0x1F926000u
#define PIC32MK_CAN4_MSGRAM_BASE 0x1F939000u
#define PIC32MK_CAN_MSGRAM_SIZE  (75u * 1024u)  /* max 74 KB, rounded up */

/*
 * USB OTG 1   _USB_BASE_ADDRESS = 0xBF889040 (p32mk1024mcm100.h / xc.h)
 * struct usb_registers_t pointer starts at 0xBF889040 (= device_base + 0x040).
 * Device MMIO window starts at 0xBF889000 so UxOTGIR lands at offset 0x040.
 */
#define PIC32MK_USB1_OFFSET     0x089000u
#define PIC32MK_USB_OFFSET      PIC32MK_USB1_OFFSET   /* legacy alias */

/* USB OTG 2   _USB2_BASE_ADDRESS = 0xBF88A040 */
#define PIC32MK_USB2_OFFSET     0x08A000u

/* RTCC                                         0xBF8C0000 */
#define PIC32MK_RTCC_OFFSET     0x0C0000u

/*
 * Reset-control registers (§7, p. 114) — offsets from SFR base
 * -----------------------------------------------------------------------
 */

#define PIC32MK_RCON_OFFSET     0x001240u   /* Reset Control */
#define PIC32MK_RSWRST_OFFSET   0x001250u   /* Software Reset trigger */
#define PIC32MK_RNMICON_OFFSET  0x001260u   /* NMI Control */
#define PIC32MK_PWRCON_OFFSET   0x001270u   /* Power Control */

/* RCON reset flags (§7 register map) */
#define PIC32MK_RCON_POR        (1u << 2)   /* Power-on Reset */
#define PIC32MK_RCON_BOR        (1u << 3)   /* Brown-out Reset */

/*
 * CFG / PMD registers (§6) — offsets from PIC32MK_CFG_OFFSET (0xBF800000)
 * -----------------------------------------------------------------------
 */

#define PIC32MK_CFGCON          0x0000u     /* Configuration control */
#define PIC32MK_SYSKEY          0x0030u     /* System key unlock */
#define PIC32MK_PMD1            0x0040u     /* Peripheral Module Disable 1 */
#define PIC32MK_PMD2            0x0050u
#define PIC32MK_PMD3            0x0060u
#define PIC32MK_PMD4            0x0070u
#define PIC32MK_PMD5            0x0080u
#define PIC32MK_PMD6            0x0090u
#define PIC32MK_PMD7            0x00A0u
#define PIC32MK_CFGCON2         0x0110u     /* Extended configuration control */
#define PIC32MK_CHECON          0x0800u     /* Prefetch Cache Control (CHECON) */

/* CFGCON bits */
#define PIC32MK_CFGCON_PGLOCK   (1u << 28)  /* Permission-group lock */
#define PIC32MK_CFGCON_PMDLOCK  (1u << 29)  /* PMD lock */
#define PIC32MK_CFGCON_IOLOCK   (1u << 30)  /* I/O lock */

/* Number of PMD registers */
#define PIC32MK_PMD_COUNT       7

/*
 * CRU registers (§9) — offsets from PIC32MK_CRU_OFFSET (0xBF801200)
 * Each register has SET/CLR/INV aliases at +4/+8/+C.
 * -----------------------------------------------------------------------
 */

/* Oscillator / PLL registers */
#define PIC32MK_CRU_OSCCON      0x00u       /* Oscillator Control */
#define PIC32MK_CRU_OSCTUN      0x10u       /* Oscillator Tuning */
#define PIC32MK_CRU_SPLLCON     0x20u       /* System PLL Control */
#define PIC32MK_CRU_UPLLCON     0x30u       /* USB PLL Control */

/* Reset-control registers (formerly in separate RCON stub) */
#define PIC32MK_CRU_RCON        0x40u       /* Reset Control */
#define PIC32MK_CRU_RSWRST      0x50u       /* Software Reset trigger */
#define PIC32MK_CRU_RNMICON     0x60u       /* NMI Control */
#define PIC32MK_CRU_PWRCON      0x70u       /* Power Control */

/* Reference clock outputs 1–4 (CON + TRIM pairs, 0x20 stride) */
#define PIC32MK_CRU_REFO1CON    0x80u
#define PIC32MK_CRU_REFO1TRIM   0x90u
#define PIC32MK_CRU_REFO2CON    0xA0u
#define PIC32MK_CRU_REFO2TRIM   0xB0u
#define PIC32MK_CRU_REFO3CON    0xC0u
#define PIC32MK_CRU_REFO3TRIM   0xD0u
#define PIC32MK_CRU_REFO4CON    0xE0u
#define PIC32MK_CRU_REFO4TRIM   0xF0u

/* Peripheral bus clock dividers 1–7 (0x10 stride) */
#define PIC32MK_CRU_PB1DIV      0x100u
#define PIC32MK_CRU_PB2DIV      0x110u
#define PIC32MK_CRU_PB3DIV      0x120u
#define PIC32MK_CRU_PB4DIV      0x130u
#define PIC32MK_CRU_PB5DIV      0x140u
#define PIC32MK_CRU_PB6DIV      0x150u
#define PIC32MK_CRU_PB7DIV      0x160u

/* Clock status */
#define PIC32MK_CRU_CLKSTAT     0x190u

/* Number of reference clocks and peripheral buses */
#define PIC32MK_CRU_NREFO       4
#define PIC32MK_CRU_NPB         7

/* OSCCON bits (§9, Register 9-1) */
#define PIC32MK_OSCCON_OSWEN    (1u << 0)
#define PIC32MK_OSCCON_SOSCEN   (1u << 1)
#define PIC32MK_OSCCON_CF       (1u << 3)
#define PIC32MK_OSCCON_SLPEN    (1u << 4)
#define PIC32MK_OSCCON_CLKLOCK  (1u << 7)
#define PIC32MK_OSCCON_NOSC_MASK  0x00000700u   /* bits [10:8] */
#define PIC32MK_OSCCON_NOSC_SHIFT 8
#define PIC32MK_OSCCON_COSC_MASK  0x00007000u   /* bits [14:12] */
#define PIC32MK_OSCCON_COSC_SHIFT 12
#define PIC32MK_OSCCON_FRCDIV_MASK  0x07000000u /* bits [26:24] */
#define PIC32MK_OSCCON_FRCDIV_SHIFT 24

/* SPLLCON bits (§9, Register 9-4) */
#define PIC32MK_SPLLCON_PLLRANGE_MASK  0x00000007u /* bits [2:0] */
#define PIC32MK_SPLLCON_PLLICLK  (1u << 7)
#define PIC32MK_SPLLCON_PLLIDIV_MASK   0x00000700u /* bits [10:8] */
#define PIC32MK_SPLLCON_PLLMULT_MASK   0x007F0000u /* bits [22:16] */
#define PIC32MK_SPLLCON_PLLODIV_MASK   0x07000000u /* bits [26:24] */

/* UPLLCON bits (§9) — same layout as SPLLCON plus UPOSCEN */
#define PIC32MK_UPLLCON_UPOSCEN  (1u << 25)

/* REFOxCON bits (§9, Register 9-16) */
#define PIC32MK_REFOCON_ROSEL_MASK  0x0000000Fu /* bits [3:0] */
#define PIC32MK_REFOCON_ACTIVE   (1u << 8)
#define PIC32MK_REFOCON_DIVSWEN  (1u << 9)
#define PIC32MK_REFOCON_RSLP     (1u << 11)
#define PIC32MK_REFOCON_OE       (1u << 12)
#define PIC32MK_REFOCON_SIDL     (1u << 13)
#define PIC32MK_REFOCON_ON       (1u << 15)
#define PIC32MK_REFOCON_RODIV_MASK  0xFFFF0000u /* bits [31:16] */

/* PBxDIV bits (§9) */
#define PIC32MK_PBDIV_PBDIV_MASK  0x0000007Fu  /* bits [6:0] */
#define PIC32MK_PBDIV_PBDIVRDY   (1u << 11)
#define PIC32MK_PBDIV_ON         (1u << 15)

/* CLKSTAT bits (§9, Register 9-31) */
#define PIC32MK_CLKSTAT_FRCRDY   (1u << 0)
#define PIC32MK_CLKSTAT_POSCRDY  (1u << 2)
#define PIC32MK_CLKSTAT_SOSCRDY  (1u << 4)
#define PIC32MK_CLKSTAT_LPRCRDY  (1u << 5)
#define PIC32MK_CLKSTAT_SPLLRDY  (1u << 7)
#define PIC32MK_CLKSTAT_UPLLRDY  (1u << 8)
#define PIC32MK_CLKSTAT_ALL_RDY  (PIC32MK_CLKSTAT_FRCRDY  | \
                                  PIC32MK_CLKSTAT_POSCRDY  | \
                                  PIC32MK_CLKSTAT_SOSCRDY  | \
                                  PIC32MK_CLKSTAT_LPRCRDY  | \
                                  PIC32MK_CLKSTAT_SPLLRDY  | \
                                  PIC32MK_CLKSTAT_UPLLRDY)

/*
 * EVIC registers (§8, p. 159) — offsets from 0xBF810000
 * -----------------------------------------------------------------------
 */

#define PIC32MK_EVIC_INTCON     0x0000u     /* MVEC, TPC, INTxEP */
#define PIC32MK_EVIC_PRISS      0x0010u     /* Priority shadow reg select */
#define PIC32MK_EVIC_INTSTAT    0x0020u     /* Last IRQ serviced, SRIPL */
#define PIC32MK_EVIC_IPTMR      0x0030u     /* Interrupt proximity timer */
#define PIC32MK_EVIC_IFS0       0x0040u     /* Interrupt flag status [0..7] */
#define PIC32MK_EVIC_IEC0       0x00C0u     /* Interrupt enable control [0..7] */
#define PIC32MK_EVIC_IPC0       0x0140u     /* Interrupt priority control [0..63] */
#define PIC32MK_EVIC_OFF0       0x0540u     /* Vector address offsets [0..190] */

/*
 * Number of interrupt sources / vectors.
 * The EVIC IFS/IEC registers span 8×32 = 256 bits; USB1 (244) and USB2 (246)
 * are in IFS7.  Use 256 to cover the full IPC/IFS table.
 */
#define PIC32MK_NUM_IRQ_SOURCES 256
#define PIC32MK_NUM_VECTORS     190

/*
 * UART1 registers (§21, Table 21-2) — U1MODE base 0xBF828000
 * Offset from SFR base: 0x028000
 * -----------------------------------------------------------------------
 */

#define PIC32MK_UART1_OFFSET    0x028000u   /* SFR offset → physical 0x1F828000 */
#define PIC32MK_UART1_SIZE      0x000050u   /* U1MODE..U1BRG+INV */

/* Register offsets within the UART block (same layout for all UARTs) */
#define PIC32MK_UxMODE          0x00u       /* Mode */
#define PIC32MK_UxSTA           0x10u       /* Status & control */
#define PIC32MK_UxTXREG         0x20u       /* TX data */
#define PIC32MK_UxRXREG         0x30u       /* RX data */
#define PIC32MK_UxBRG           0x40u       /* Baud rate */

/* U1STA bits relevant to TX polling */
#define PIC32MK_USTA_TRMT       (1u << 8)   /* TX shift register empty (1=empty) */
#define PIC32MK_USTA_UTXBF      (1u << 9)   /* TX buffer full (0=not full) */

/*
 * CPU clock (§3)
 * -----------------------------------------------------------------------
 */

#define PIC32MK_CPU_HZ          120000000u  /* 120 MHz max */

/*
 * Interrupt source numbers (§8, Table 8-1, DS60001519E)
 * TODO: verify each number against the actual datasheet table.
 * -----------------------------------------------------------------------
 */

#define PIC32MK_IRQ_CT          0    /* Core Timer */
#define PIC32MK_IRQ_CS0         1    /* Core Software Interrupt 0 */
#define PIC32MK_IRQ_CS1         2    /* Core Software Interrupt 1 */
#define PIC32MK_IRQ_INT0        3    /* External Interrupt 0 */
#define PIC32MK_IRQ_T1          4    /* Timer 1 */
#define PIC32MK_IRQ_T2          9    /* Timer 2 */
#define PIC32MK_IRQ_T3          14   /* Timer 3 */
#define PIC32MK_IRQ_T4          19   /* Timer 4 */
#define PIC32MK_IRQ_T5          24   /* Timer 5 */
#define PIC32MK_IRQ_T6          28   /* Timer 6 */
#define PIC32MK_IRQ_T7          32   /* Timer 7 */
#define PIC32MK_IRQ_T8          36   /* Timer 8 */
#define PIC32MK_IRQ_T9          40   /* Timer 9 */
/* UART1: error=38, RX=39, TX=40  (IFS1 bits 6/7/8, p32mk1024mcm100.h) */
#define PIC32MK_IRQ_U1E         38
#define PIC32MK_IRQ_U1RX        39
#define PIC32MK_IRQ_U1TX        40
#define PIC32MK_IRQ_U2E         115
#define PIC32MK_IRQ_U2RX        116
#define PIC32MK_IRQ_U2TX        117
#define PIC32MK_IRQ_U3E         118
#define PIC32MK_IRQ_U3RX        119
#define PIC32MK_IRQ_U3TX        120
#define PIC32MK_IRQ_U4E         121
#define PIC32MK_IRQ_U4RX        122
#define PIC32MK_IRQ_U4TX        123
#define PIC32MK_IRQ_U5E         124
#define PIC32MK_IRQ_U5RX        125
#define PIC32MK_IRQ_U5TX        126
#define PIC32MK_IRQ_U6E         127
#define PIC32MK_IRQ_U6RX        128
#define PIC32MK_IRQ_U6TX        129
/* CAN FD — single IRQ per instance (§8, Table 8-1, DS60001519E) */
#define PIC32MK_IRQ_CAN1        167
#define PIC32MK_IRQ_CAN2        168
#define PIC32MK_IRQ_CAN3        187
#define PIC32MK_IRQ_CAN4        188

/*
 * USB OTG — interrupt vector numbers from XC32 p32mk1024mcm100.h
 * USB1: vector 34 → IFS1 bit 2, IPC8[18:16]
 * USB2: vector 244 → IFS7 bit 20, IPC61[2:0]
 */
#define PIC32MK_IRQ_USB1        34
#define PIC32MK_IRQ_USB2        244

/* DMA channels 0-7 */
#define PIC32MK_IRQ_DMA0        134
#define PIC32MK_IRQ_DMA1        135
#define PIC32MK_IRQ_DMA2        136
#define PIC32MK_IRQ_DMA3        137
#define PIC32MK_IRQ_DMA4        138
#define PIC32MK_IRQ_DMA5        139
#define PIC32MK_IRQ_DMA6        140
#define PIC32MK_IRQ_DMA7        141

/*
 * GPIO Change-Notice interrupt vectors (DS60001519E Table 8-1,
 * _CHANGE_NOTICE_x_VECTOR from p32mk1024mcm100.h)
 */
#define PIC32MK_IRQ_CNA         44
#define PIC32MK_IRQ_CNB         45
#define PIC32MK_IRQ_CNC         46
#define PIC32MK_IRQ_CND         47
#define PIC32MK_IRQ_CNE         48
#define PIC32MK_IRQ_CNF         49
#define PIC32MK_IRQ_CNG         50

/*
 * Timer peripheral registers (§14, DS60001519E)
 * Offsets from PIC32MK_PER1_OFFSET (0xBF820000).
 * Each timer block is 0x200 bytes; register stride 0x10 (with SET/CLR/INV).
 * TODO: verify exact base offsets for PIC32MK GPK from Table 4-2.
 * -----------------------------------------------------------------------
 */

/* Timer base offsets from SFR base */
#define PIC32MK_T1_OFFSET       0x020000u   /* Timer 1 (Type A, 16-bit) */
#define PIC32MK_T2_OFFSET       0x020200u   /* Timer 2 (Type B) */
#define PIC32MK_T3_OFFSET       0x020400u   /* Timer 3 (Type C) */
#define PIC32MK_T4_OFFSET       0x020600u   /* Timer 4 (Type B) */
#define PIC32MK_T5_OFFSET       0x020800u   /* Timer 5 (Type C) */
#define PIC32MK_T6_OFFSET       0x020A00u   /* Timer 6 (Type B) */
#define PIC32MK_T7_OFFSET       0x020C00u   /* Timer 7 (Type C) */
#define PIC32MK_T8_OFFSET       0x020E00u   /* Timer 8 (Type B) */
#define PIC32MK_T9_OFFSET       0x021000u   /* Timer 9 (Type C) */

#define PIC32MK_TIMER_BLOCK_SIZE    0x200u  /* per-timer SFR block */

/* Timer register offsets within block (with SET/CLR/INV at +4/+8/+C) */
#define PIC32MK_TxCON           0x00u   /* Control: ON, TCKPS, T32, TCS... */
#define PIC32MK_TMRx            0x10u   /* Current count */
#define PIC32MK_PRx             0x20u   /* Period register */

/* TxCON bits */
#define PIC32MK_TCON_ON         (1u << 15)  /* Timer ON */
#define PIC32MK_TCON_T32        (1u << 3)   /* 32-bit mode (Type B only) */
#define PIC32MK_TCON_TCS        (1u << 1)   /* Clock source select */
#define PIC32MK_TCON_TCKPS_MASK 0x0070u     /* Prescaler bits [6:4] */
#define PIC32MK_TCON_TCKPS_SHIFT    4

/*
 * OC (Output Compare) register offsets from SFR base (§19, DS60001519E)
 * OC1-OC9:  0xBF824000..0xBF825000 (0x200 stride)
 * OC10-OC16: 0xBF845200..0xBF845E00 (0x200 stride)
 * -----------------------------------------------------------------------
 */
#define PIC32MK_OC1_OFFSET      0x024000u
#define PIC32MK_OC2_OFFSET      0x024200u
#define PIC32MK_OC3_OFFSET      0x024400u
#define PIC32MK_OC4_OFFSET      0x024600u
#define PIC32MK_OC5_OFFSET      0x024800u
#define PIC32MK_OC6_OFFSET      0x024A00u
#define PIC32MK_OC7_OFFSET      0x024C00u
#define PIC32MK_OC8_OFFSET      0x024E00u
#define PIC32MK_OC9_OFFSET      0x025000u
#define PIC32MK_OC10_OFFSET     0x045200u
#define PIC32MK_OC11_OFFSET     0x045400u
#define PIC32MK_OC12_OFFSET     0x045600u
#define PIC32MK_OC13_OFFSET     0x045800u
#define PIC32MK_OC14_OFFSET     0x045A00u
#define PIC32MK_OC15_OFFSET     0x045C00u
#define PIC32MK_OC16_OFFSET     0x045E00u

#define PIC32MK_OC_BLOCK_SIZE   0x200u

/* OC register offsets within each 0x200-byte block */
#define PIC32MK_OCxCON          0x00u   /* Control */
#define PIC32MK_OCxR            0x10u   /* Primary compare value */
#define PIC32MK_OCxRS           0x20u   /* Secondary compare value */

/* OCxCON bits (Register 19-1) */
#define PIC32MK_OCCON_ON        (1u << 15)
#define PIC32MK_OCCON_SIDL      (1u << 13)
#define PIC32MK_OCCON_OC32      (1u << 5)
#define PIC32MK_OCCON_OCFLT     (1u << 4)
#define PIC32MK_OCCON_OCTSEL    (1u << 3)
#define PIC32MK_OCCON_OCM_MASK  0x0007u
#define PIC32MK_OCCON_OCM_SHIFT 0

/* OC interrupt vector numbers */
#define PIC32MK_IRQ_OC1         7
#define PIC32MK_IRQ_OC2         12
#define PIC32MK_IRQ_OC3         17
#define PIC32MK_IRQ_OC4         22
#define PIC32MK_IRQ_OC5         27
#define PIC32MK_IRQ_OC6         79
#define PIC32MK_IRQ_OC7         83
#define PIC32MK_IRQ_OC8         87
#define PIC32MK_IRQ_OC9         91
#define PIC32MK_IRQ_OC10        199
#define PIC32MK_IRQ_OC11        202
#define PIC32MK_IRQ_OC12        205
#define PIC32MK_IRQ_OC13        208
#define PIC32MK_IRQ_OC14        211
#define PIC32MK_IRQ_OC15        214
#define PIC32MK_IRQ_OC16        217

/*
 * Input Capture (IC1–IC16) — §18, DS60001519E
 * IC1–IC9:   SFR bank 1, base 0xBF822000, stride 0x200
 * IC10–IC16: SFR bank 2, base 0xBF843200, stride 0x200
 * -----------------------------------------------------------------------
 */
#define PIC32MK_IC1_OFFSET      0x022000u
#define PIC32MK_IC2_OFFSET      0x022200u
#define PIC32MK_IC3_OFFSET      0x022400u
#define PIC32MK_IC4_OFFSET      0x022600u
#define PIC32MK_IC5_OFFSET      0x022800u
#define PIC32MK_IC6_OFFSET      0x022A00u
#define PIC32MK_IC7_OFFSET      0x022C00u
#define PIC32MK_IC8_OFFSET      0x022E00u
#define PIC32MK_IC9_OFFSET      0x023000u
#define PIC32MK_IC10_OFFSET     0x043200u
#define PIC32MK_IC11_OFFSET     0x043400u
#define PIC32MK_IC12_OFFSET     0x043600u
#define PIC32MK_IC13_OFFSET     0x043800u
#define PIC32MK_IC14_OFFSET     0x043A00u
#define PIC32MK_IC15_OFFSET     0x043C00u
#define PIC32MK_IC16_OFFSET     0x043E00u

#define PIC32MK_IC_BLOCK_SIZE   0x200u

/* IC register offsets within each block */
#define PIC32MK_ICxCON          0x00u   /* Control (+ SET/CLR/INV at +4/+8/+C) */
#define PIC32MK_ICxBUF          0x10u   /* Capture buffer (read-only FIFO pop) */

/* ICxCON bits (Register 18-1, DS60001519E) */
#define PIC32MK_ICCON_ON        (1u << 15)  /* Module enable */
#define PIC32MK_ICCON_SIDL      (1u << 13)  /* Stop in idle */
#define PIC32MK_ICCON_FEDGE     (1u << 9)   /* First edge select */
#define PIC32MK_ICCON_C32       (1u << 8)   /* 32-bit capture mode */
#define PIC32MK_ICCON_ICTMR     (1u << 7)   /* Timer source select */
#define PIC32MK_ICCON_ICI_MASK  0x0060u     /* Interrupt on every Nth capture [6:5] */
#define PIC32MK_ICCON_ICI_SHIFT 5
#define PIC32MK_ICCON_ICOV      (1u << 4)   /* Overflow (buffer full) */
#define PIC32MK_ICCON_ICBNE     (1u << 3)   /* Buffer not empty */
#define PIC32MK_ICCON_ICM_MASK  0x0007u     /* Capture mode [2:0] */

/* IC IRQ numbers (Table 8-3, DS60001519E) — paired: error IRQ, capture IRQ */
#define PIC32MK_IRQ_IC1E        5
#define PIC32MK_IRQ_IC1         6
#define PIC32MK_IRQ_IC2E        10
#define PIC32MK_IRQ_IC2         11
#define PIC32MK_IRQ_IC3E        15
#define PIC32MK_IRQ_IC3         16
#define PIC32MK_IRQ_IC4E        20
#define PIC32MK_IRQ_IC4         21
#define PIC32MK_IRQ_IC5E        25
#define PIC32MK_IRQ_IC5         26
#define PIC32MK_IRQ_IC6E        77
#define PIC32MK_IRQ_IC6         78
#define PIC32MK_IRQ_IC7E        81
#define PIC32MK_IRQ_IC7         82
#define PIC32MK_IRQ_IC8E        85
#define PIC32MK_IRQ_IC8         86
#define PIC32MK_IRQ_IC9E        89
#define PIC32MK_IRQ_IC9         90
#define PIC32MK_IRQ_IC10E       197
#define PIC32MK_IRQ_IC10        198
#define PIC32MK_IRQ_IC11E       200
#define PIC32MK_IRQ_IC11        201
#define PIC32MK_IRQ_IC12E       203
#define PIC32MK_IRQ_IC12        204
#define PIC32MK_IRQ_IC13E       206
#define PIC32MK_IRQ_IC13        207
#define PIC32MK_IRQ_IC14E       209
#define PIC32MK_IRQ_IC14        210
#define PIC32MK_IRQ_IC15E       212
#define PIC32MK_IRQ_IC15        213
#define PIC32MK_IRQ_IC16E       215
#define PIC32MK_IRQ_IC16        216

/* SPI1-6 IRQ numbers (Table 8-3, DS60001519E) */
#define PIC32MK_IRQ_SPI1_FAULT  35
#define PIC32MK_IRQ_SPI1_RX     36
#define PIC32MK_IRQ_SPI1_TX     37
#define PIC32MK_IRQ_SPI2_FAULT  53
#define PIC32MK_IRQ_SPI2_RX     54
#define PIC32MK_IRQ_SPI2_TX     55
#define PIC32MK_IRQ_SPI3_FAULT  154
#define PIC32MK_IRQ_SPI3_RX     155
#define PIC32MK_IRQ_SPI3_TX     156
#define PIC32MK_IRQ_SPI4_FAULT  166
#define PIC32MK_IRQ_SPI4_RX     167
#define PIC32MK_IRQ_SPI4_TX     168
#define PIC32MK_IRQ_SPI5_FAULT  169
#define PIC32MK_IRQ_SPI5_RX     170
#define PIC32MK_IRQ_SPI5_TX     171
#define PIC32MK_IRQ_SPI6_FAULT  172
#define PIC32MK_IRQ_SPI6_RX     173
#define PIC32MK_IRQ_SPI6_TX     174

/*
 * UART2–6 register base offsets from SFR base
 * UART1 is already defined as PIC32MK_UART1_OFFSET = 0x028000
 * TODO: verify exact offsets against DS60001519E Table 4-2.
 * -----------------------------------------------------------------------
 */

#define PIC32MK_UART2_OFFSET    0x028200u   /* UART2 (PER1 block) */
#define PIC32MK_UART3_OFFSET    0x048400u   /* UART3 (PER2 block, 0xBF848400) */
#define PIC32MK_UART4_OFFSET    0x048600u   /* UART4 (0xBF848600) */
#define PIC32MK_UART5_OFFSET    0x048800u   /* UART5 (0xBF848800) */
#define PIC32MK_UART6_OFFSET    0x048A00u   /* UART6 (0xBF848A00) */

#define PIC32MK_UART_BLOCK_SIZE 0x200u      /* per-UART SFR block */

/* Additional UxSTA bits */
#define PIC32MK_USTA_URXDA      (1u << 0)   /* RX data available */
#define PIC32MK_USTA_OERR       (1u << 1)   /* Overrun error */
#define PIC32MK_USTA_FERR       (1u << 2)   /* Framing error */
#define PIC32MK_USTA_PERR       (1u << 3)   /* Parity error */
#define PIC32MK_USTA_RIDLE      (1u << 4)   /* Receiver idle */
#define PIC32MK_USTA_UTXEN      (1u << 10)  /* TX enable */
#define PIC32MK_USTA_UTXISEL1   (1u << 14)  /* TX interrupt select */
#define PIC32MK_USTA_URXEN      (1u << 12)  /* RX enable */
#define PIC32MK_USTA_URXISEL1   (1u << 6)   /* RX interrupt select */
#define PIC32MK_UMODE_ON        (1u << 15)  /* UART enable */

/*
 * SPI peripheral registers (§23, DS60001519E)
 * TODO: verify exact base offsets.
 * -----------------------------------------------------------------------
 */

#define PIC32MK_SPI1_OFFSET     0x021800u
#define PIC32MK_SPI2_OFFSET     0x021A00u
#define PIC32MK_SPI3_OFFSET     0x040800u
#define PIC32MK_SPI4_OFFSET     0x040A00u
#define PIC32MK_SPI5_OFFSET     0x047800u   /* 0xBF847800 */
#define PIC32MK_SPI6_OFFSET     0x040E00u
#define PIC32MK_SPI_BLOCK_SIZE  0x200u

/* SPI register offsets within block */
#define PIC32MK_SPIxCON         0x00u
#define PIC32MK_SPIxSTAT        0x10u
#define PIC32MK_SPIxBUF         0x20u
#define PIC32MK_SPIxBRG         0x30u
#define PIC32MK_SPIxCON2        0x40u

/*
 * I2C peripheral registers (§24, DS60001519E)
 * TODO: verify exact base offsets.
 * -----------------------------------------------------------------------
 */

#define PIC32MK_I2C1_OFFSET     0x026000u   /* 0xBF826000 — §24, Table 4-2 */
#define PIC32MK_I2C2_OFFSET     0x026200u   /* 0xBF826200 */
#define PIC32MK_I2C3_OFFSET     0x046400u   /* 0xBF846400 (PER2 bank) */
#define PIC32MK_I2C4_OFFSET     0x046600u   /* 0xBF846600 */
#define PIC32MK_I2C_BLOCK_SIZE  0x200u

/* I2C register offsets within block */
#define PIC32MK_I2CxCON        0x00u
#define PIC32MK_I2CxSTAT       0x10u
#define PIC32MK_I2CxADD        0x20u
#define PIC32MK_I2CxMSK        0x30u
#define PIC32MK_I2CxTRN        0x40u
#define PIC32MK_I2CxRCV        0x50u

/*
 * GPIO peripheral registers (§12, DS60001519E)
 * Base: PIC32MK_GPIO_OFFSET = 0x060000 (0xBF860000)
 * Each port (A–G) occupies 0x100 bytes.
 * -----------------------------------------------------------------------
 */

#define PIC32MK_GPIO_PORT_SIZE  0x100u      /* per-port register block */

/* GPIO register offsets within each port block */
#define PIC32MK_ANSEL           0x00u   /* Analog select */
#define PIC32MK_TRIS            0x10u   /* Direction (1=input) */
#define PIC32MK_PORT            0x20u   /* Read pin state */
#define PIC32MK_LAT             0x30u   /* Latch (write output) */
#define PIC32MK_ODC             0x40u   /* Open-drain control */
#define PIC32MK_CNPU            0x50u   /* Change-notice pull-up */
#define PIC32MK_CNPD            0x60u   /* Change-notice pull-down */
#define PIC32MK_CNCON           0x70u   /* Change-notice control */
#define PIC32MK_CNEN0           0x80u   /* CN edge enable 0 */
#define PIC32MK_CNSTAT          0x90u   /* CN status */
#define PIC32MK_CNEN1           0xA0u   /* CN edge enable 1 */
#define PIC32MK_CNF             0xB0u   /* CN flag */

/* Number of GPIO ports (A–G) */
#define PIC32MK_GPIO_NPORTS     7

/*
 * Set shared chardev for GPIO state-change event streaming.
 * Called from board init; multiple ports share the same Chardev*.
 */
void pic32mk_gpio_set_chardev(DeviceState *dev, Chardev *chr);
void pic32mk_oc_set_chardev(DeviceState *dev, Chardev *chr);

/*
 * DMA controller registers (§26, DS60001519E)
 * Base: PIC32MK_DMA_OFFSET = 0x011000 (within EVIC page, 0xBF811000)
 * Global registers at base, then 8 channel blocks at +0x60 each.
 * -----------------------------------------------------------------------
 */

/* DMA global registers */
#define PIC32MK_DMACON_OFFSET   0x00u   /* DMA control */
#define PIC32MK_DMASTAT_OFFSET  0x10u   /* DMA status */
#define PIC32MK_DMAADDR_OFFSET  0x20u   /* DMA address */

/* DMA channel registers base: +0x60 + channel * 0xC0 */
#define PIC32MK_DMA_CH_BASE     0x60u
#define PIC32MK_DMA_CH_STRIDE   0xC0u
#define PIC32MK_DMA_NCHANNELS   8

/* DMA channel register offsets within each channel block */
#define PIC32MK_DCHxCON         0x00u
#define PIC32MK_DCHxECON        0x10u
#define PIC32MK_DCHxINT         0x20u
#define PIC32MK_DCHxSSA         0x30u
#define PIC32MK_DCHxDSA         0x40u
#define PIC32MK_DCHxSSIZ        0x50u
#define PIC32MK_DCHxDSIZ        0x60u
#define PIC32MK_DCHxSPTR        0x70u
#define PIC32MK_DCHxDPTR        0x80u
#define PIC32MK_DCHxCSIZ        0x90u
#define PIC32MK_DCHxCPTR        0xA0u
#define PIC32MK_DCHxDAT         0xB0u

/*
 * ADCHS peripheral registers (§22, DS60001519E)
 * Base: 0xBF887000 (SFR offset 0x087000)
 * Register block spans ~4 KB (0x000–0xE1C).
 * Each register has SET/CLR/INV aliases at +4/+8/+C.
 * -----------------------------------------------------------------------
 */

#define PIC32MK_ADC_OFFSET      0x087000u   /* 0xBF887000 */
#define PIC32MK_ADC_SIZE        0x001000u   /* 4 KB register window */

/* Control registers */
#define PIC32MK_ADCCON1         0x000u
#define PIC32MK_ADCCON2         0x010u
#define PIC32MK_ADCCON3         0x020u
#define PIC32MK_ADCTRGMODE      0x030u

/* Input mode control (signed/unsigned per channel group) */
#define PIC32MK_ADCIMCON1       0x040u
#define PIC32MK_ADCIMCON2       0x050u
#define PIC32MK_ADCIMCON3       0x060u
#define PIC32MK_ADCIMCON4       0x070u

/* Global interrupt enable (result ready, 2 × 32 bits) */
#define PIC32MK_ADCGIRQEN1      0x080u
#define PIC32MK_ADCGIRQEN2      0x090u

/* Channel scan select */
#define PIC32MK_ADCCSS1         0x0A0u
#define PIC32MK_ADCCSS2         0x0B0u

/* Data ready status */
#define PIC32MK_ADCDSTAT1       0x0C0u
#define PIC32MK_ADCDSTAT2       0x0D0u

/* Compare enable */
#define PIC32MK_ADCCMPEN1       0x0E0u
#define PIC32MK_ADCCMPEN2       0x100u
#define PIC32MK_ADCCMPEN3       0x120u
#define PIC32MK_ADCCMPEN4       0x140u

/* Compare values */
#define PIC32MK_ADCCMP1         0x0F0u
#define PIC32MK_ADCCMP2         0x110u
#define PIC32MK_ADCCMP3         0x130u
#define PIC32MK_ADCCMP4         0x150u

/* Digital filter registers */
#define PIC32MK_ADCFLTR1        0x1A0u
#define PIC32MK_ADCFLTR2        0x1B0u
#define PIC32MK_ADCFLTR3        0x1C0u
#define PIC32MK_ADCFLTR4        0x1D0u

/* Trigger configuration */
#define PIC32MK_ADCTRG1         0x200u
#define PIC32MK_ADCTRG2         0x210u
#define PIC32MK_ADCTRG3         0x220u
#define PIC32MK_ADCTRG4         0x230u
#define PIC32MK_ADCTRG5         0x240u
#define PIC32MK_ADCTRG6         0x250u
#define PIC32MK_ADCTRG7         0x260u

/* Compare control */
#define PIC32MK_ADCCMPCON1      0x280u
#define PIC32MK_ADCCMPCON2      0x290u
#define PIC32MK_ADCCMPCON3      0x2A0u
#define PIC32MK_ADCCMPCON4      0x2B0u

/* Misc registers */
#define PIC32MK_ADCBASE         0x300u
#define PIC32MK_ADCTRGSNS       0x340u

/* Sampling time (per-module) */
#define PIC32MK_ADC0TIME        0x350u
#define PIC32MK_ADC1TIME        0x360u
#define PIC32MK_ADC2TIME        0x370u
#define PIC32MK_ADC3TIME        0x380u
#define PIC32MK_ADC4TIME        0x390u
#define PIC32MK_ADC5TIME        0x3A0u

/* Early interrupt enable / status */
#define PIC32MK_ADCEIEN1        0x3C0u
#define PIC32MK_ADCEIEN2        0x3D0u
#define PIC32MK_ADCEISTAT1      0x3E0u
#define PIC32MK_ADCEISTAT2      0x3F0u

/* Analog module enable / warm-up control */
#define PIC32MK_ADCANCON        0x400u

/* Conversion data registers (stride 0x10 per channel index) */
#define PIC32MK_ADCDATA_BASE    0x600u
#define PIC32MK_ADCDATA_STRIDE  0x010u

/* Per-module configuration */
#define PIC32MK_ADC0CFG         0xD00u
#define PIC32MK_ADC1CFG         0xD10u
#define PIC32MK_ADC2CFG         0xD20u
#define PIC32MK_ADC3CFG         0xD30u
#define PIC32MK_ADC4CFG         0xD40u
#define PIC32MK_ADC5CFG         0xD50u
#define PIC32MK_ADC6CFG         0xD60u
#define PIC32MK_ADC7CFG         0xD70u

/* System configuration */
#define PIC32MK_ADCSYSCFG0      0xE00u
#define PIC32MK_ADCSYSCFG1      0xE10u

/* Maximum channel index (0–53, with gaps) */
#define PIC32MK_ADC_MAX_CH      54

/* ADCCON1 bits */
#define PIC32MK_ADCCON1_ON      (1u << 15)

/* ADCCON2 bits */
#define PIC32MK_ADCCON2_BGVRRDY (1u << 31)  /* Band-gap voltage ref ready */
#define PIC32MK_ADCCON2_REFFLT  (1u << 30)  /* Reference fault */

/* ADCCON3 bits */
#define PIC32MK_ADCCON3_ADINSEL_MASK  0x3Fu        /* bits [5:0] channel select */
#define PIC32MK_ADCCON3_RQCNVRT (1u << 8)          /* Request conversion */
#define PIC32MK_ADCCON3_GSWTRG  (1u << 6)          /* Global software trigger */
#define PIC32MK_ADCCON3_GLSWTRG (1u << 5)          /* Global level SW trigger */
#define PIC32MK_ADCCON3_DIGEN_SHIFT  16             /* DIGENx at bits [23:16] */

/* ADCANCON bits — ANENx and WKRDYx (modules 0–5, 7) */
#define PIC32MK_ADCANCON_ANEN_SHIFT   0             /* ANENx at bits [7:0] */
#define PIC32MK_ADCANCON_WKRDY_SHIFT  8             /* WKRDYx at bits [15:8] */

/*
 * ADCHS interrupt source numbers (§8, Table 8-1) — vector/IRQ numbers
 * -----------------------------------------------------------------------
 */

#define PIC32MK_IRQ_ADC         92   /* Main ADC interrupt */
#define PIC32MK_IRQ_ADC_DC1     94   /* Digital comparator 1 */
#define PIC32MK_IRQ_ADC_DC2     95   /* Digital comparator 2 */
#define PIC32MK_IRQ_ADC_DF1     96   /* Digital filter 1 */
#define PIC32MK_IRQ_ADC_DF2     97   /* Digital filter 2 */
#define PIC32MK_IRQ_ADC_DF3     98   /* Digital filter 3 */
#define PIC32MK_IRQ_ADC_DF4     99   /* Digital filter 4 */
#define PIC32MK_IRQ_ADC_FAULT   100  /* ADC fault */
#define PIC32MK_IRQ_ADC_EOS     101  /* End of scan */
#define PIC32MK_IRQ_ADC_ARDY    102  /* Analog ready */
#define PIC32MK_IRQ_ADC_URDY    103  /* Update ready */
#define PIC32MK_IRQ_ADC_DMA     104  /* DMA */
#define PIC32MK_IRQ_ADC_EARLY   105  /* Early interrupt */
#define PIC32MK_IRQ_ADC_DATA0   106  /* Data ready channel 0 (base) */
/* DATA1..DATA27 = 107..133, DATA33..41 = 139..147, DATA45..53 = 151..159 */
#define PIC32MK_IRQ_ADC_DC3     245  /* Digital comparator 3 */
#define PIC32MK_IRQ_ADC_DC4     246  /* Digital comparator 4 */

/*
 * NVM / Flash Controller (§10, DS60001519E)
 * Base: 0xBF800A00 (SFR offset 0x000A00)
 * Register block: NVMCON, NVMKEY, NVMADDR, NVMDATA0–3, NVMSRCADDR,
 * NVMPWP, NVMBWP, NVMCON2 (each 0x10 stride with SET/CLR/INV aliases,
 * except NVMKEY which is write-only, no aliases).
 * -----------------------------------------------------------------------
 */

#define PIC32MK_NVM_OFFSET      0x000A00u   /* 0xBF800A00 */
#define PIC32MK_NVM_SIZE        0x0000B0u   /* NVMCON..NVMCON2+INV inclusive */

/* Register offsets within the NVM block */
#define PIC32MK_NVMCON          0x00u       /* +CLR/SET/INV at +4/+8/+C */
#define PIC32MK_NVMKEY          0x10u
#define PIC32MK_NVMADDR         0x20u       /* +CLR/SET/INV */
#define PIC32MK_NVMDATA0        0x30u       /* +CLR/SET/INV */
#define PIC32MK_NVMDATA1        0x40u
#define PIC32MK_NVMDATA2        0x50u
#define PIC32MK_NVMDATA3        0x60u
#define PIC32MK_NVMSRCADDR      0x70u       /* +CLR/SET/INV */
#define PIC32MK_NVMPWP          0x80u
#define PIC32MK_NVMBWP          0x90u
#define PIC32MK_NVMCON2         0xA0u

/* NVMCON bits */
#define PIC32MK_NVMCON_NVMOP_MASK  0x000Fu  /* bits [3:0] */
#define PIC32MK_NVMCON_BFSWAP     (1u << 6)
#define PIC32MK_NVMCON_PFSWAP     (1u << 7)
#define PIC32MK_NVMCON_LVDERR     (1u << 12)
#define PIC32MK_NVMCON_WRERR      (1u << 13)
#define PIC32MK_NVMCON_WREN       (1u << 14)
#define PIC32MK_NVMCON_WR         (1u << 15)

/* NVM operation codes (NVMOP field, bits [3:0]) */
#define PIC32MK_NVMOP_NOP              0x0u
#define PIC32MK_NVMOP_WORD_PROG        0x1u
#define PIC32MK_NVMOP_QUAD_WORD_PROG   0x2u
#define PIC32MK_NVMOP_ROW_PROG         0x3u
#define PIC32MK_NVMOP_PAGE_ERASE       0x4u
#define PIC32MK_NVMOP_LOWER_PFM_ERASE  0x5u
#define PIC32MK_NVMOP_UPPER_PFM_ERASE  0x6u
#define PIC32MK_NVMOP_PFM_ERASE        0x7u

/* Unlock keys (written sequentially to NVMKEY) */
#define PIC32MK_NVMKEY1         0xAA996655u
#define PIC32MK_NVMKEY2         0x556699AAu

/* Flash geometry */
#define PIC32MK_NVM_PAGE_SIZE   4096u       /* erase granularity */
#define PIC32MK_NVM_ROW_SIZE    512u        /* write-row granularity */

/* Interrupt — Flash Control Error vector 31 */
#define PIC32MK_IRQ_FCE         31

/*
 * Data EEPROM (§11, DS60001519E)
 * Base: 0xBF829000 (SFR offset 0x029000)
 * Register block: EECON, EEKEY, EEADDR, EEDATA (each 0x10 stride with
 * SET/CLR/INV aliases, except EEKEY which is write-only, no aliases).
 * -----------------------------------------------------------------------
 */

#define PIC32MK_DATAEE_OFFSET   0x029000u   /* 0xBF829000 */
#define PIC32MK_DATAEE_SIZE     0x000040u   /* EECON..EEDATA+INV inclusive */

/* Register offsets within the DATAEE block */
#define PIC32MK_EECON           0x00u
#define PIC32MK_EEKEY           0x10u
#define PIC32MK_EEADDR          0x20u
#define PIC32MK_EEDATA_REG      0x30u       /* "_REG" avoids clash with EEDATA macro */

/* EECON bits (p32mk1024mcm100.h) */
#define PIC32MK_EECON_CMD_MASK  0x00000007u /* bits [2:0] */
#define PIC32MK_EECON_CMD_SHIFT 0
#define PIC32MK_EECON_ILW       (1u << 3)
#define PIC32MK_EECON_ERR_MASK  0x00000030u /* bits [5:4] */
#define PIC32MK_EECON_ERR_SHIFT 4
#define PIC32MK_EECON_WREN      (1u << 6)
#define PIC32MK_EECON_RW        (1u << 7)
#define PIC32MK_EECON_ABORT     (1u << 12)
#define PIC32MK_EECON_SIDL      (1u << 13)
#define PIC32MK_EECON_RDY       (1u << 14)
#define PIC32MK_EECON_ON        (1u << 15)

/* EEADDR valid bits — 14-bit, word-aligned */
#define PIC32MK_EEADDR_MASK     0x00003FFCu

/* EEPROM storage geometry */
#define PIC32MK_DATAEE_WORDS        1024    /* 4 KB = 1024 × 32-bit words */
#define PIC32MK_DATAEE_PAGE_WORDS   32      /* 128-byte page */

/* Unlock keys (written sequentially to EEKEY) */
#define PIC32MK_EEKEY1          0xEDB7u
#define PIC32MK_EEKEY2          0x1248u

/* EECON CMD field values */
#define PIC32MK_EECMD_WORD_READ     0
#define PIC32MK_EECMD_WORD_WRITE    1
#define PIC32MK_EECMD_PAGE_ERASE    2
#define PIC32MK_EECMD_BULK_ERASE    3
#define PIC32MK_EECMD_CONFIG_WRITE  4

/* Interrupt — Data EEPROM vector 186 */
#define PIC32MK_IRQ_DATAEE      186

#endif /* HW_MIPS_PIC32MK_H */
