/*
 * pic32mk_test_fw.S — Minimal boot firmware for PIC32MK functional test
 *
 * Loaded by QEMU at physical 0x1FC40000 (KSEG1: 0xBFC40000) via -bios.
 * The board reset vector at 0xBFC00000 has a j 0xBFC40000 trampoline.
 *
 * Writes "Hello, PIC32MK!\r\n" to UART1 TX register then spins.
 * The functional test waits for this string on the serial console.
 *
 * UART1 SFR addresses (DS60001519E §21, Table 21-2):
 *   U1MODE  0xBF828000  bit 15 = UARTEN
 *   U1STA   0xBF828010  bit 10 = UTXEN
 *   U1TXREG 0xBF828020  TX data register (write-only)
 *   U1BRG   0xBF828040  baud rate divisor
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .section .text
    .set    noreorder
    .set    mips32r2
    .global _start
    .type   _start, @function
_start:
    /* Disable all interrupts, enter kernel mode */
    mtc0    $zero, $12          /* CP0 Status = 0 */
    nop

    /* Stack pointer = top of KSEG1 RAM (physical 256 KB = 0x00040000) */
    lui     $sp, 0xA004         /* 0xA0040000 */
    addiu   $sp, $sp, -8

    /* ---- UART1 init ---- */
    lui     $t0, 0xBF82
    ori     $t0, $t0, 0x8000   /* t0 = 0xBF828000 (U1MODE base) */

    addiu   $t1, $zero, 64
    sw      $t1, 0x40($t0)     /* U1BRG  = 64   (baud ignored by emulator) */
    ori     $t1, $zero, 0x0400
    sw      $t1, 0x10($t0)     /* U1STA  = UTXEN (bit 10) */
    lui     $t1, 0x8000
    sw      $t1, 0x00($t0)     /* U1MODE = UARTEN (bit 15) */

    /* ---- Transmit string byte-by-byte ---- */
    addiu   $t2, $t0, 0x20     /* t2 = U1TXREG = 0xBF828020 */
    la      $t1, _msg
_putc:
    lbu     $t3, 0($t1)
    beq     $t3, $zero, _halt
    nop
    sw      $t3, 0($t2)
    addiu   $t1, $t1, 1
    b       _putc
    nop

_halt:
    wait
    b       _halt
    nop

    .size _start, . - _start

_msg:
    .ascii  "Hello, PIC32MK!\r\n\0"
