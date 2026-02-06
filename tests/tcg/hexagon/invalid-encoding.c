/*
 * Test that invalid non-duplex encodings are properly rejected.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The encoding 0xffffc000 has parse bits [15:14] = 0b11, making it a
 * non-duplex instruction and packet end.  The remaining bits do not match
 * any valid normal or HVX instruction encoding, so this should raise SIGILL.
 */

int main()
{
    asm volatile(
        ".word 0xffffc000\n"
        : : : "memory");
    return 0;
}
