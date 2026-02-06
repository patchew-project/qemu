/*
 * Test that invalid duplex encodings are properly rejected.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * This test uses an invalid packet encoding where:
 * - Word 0: 0x0fff6fff = immext(#0xfffbffc0), parse bits = 01
 * - Word 1: 0x600237b0 = duplex with:
 *     - slot0 = 0x17b0 (invalid S2 subinstruction encoding)
 *     - slot1 = 0x0002 (valid SA1_addi)
 *     - duplex iclass = 7 (S2 for slot0, A for slot1)
 *
 * Since slot0 doesn't decode to any valid S2 subinstruction, this packet
 * should be rejected and raise SIGILL.
 */

int main()
{
    asm volatile(
        /* Invalid packet: immext followed by duplex with invalid slot0 */
        ".word 0x0fff6fff\n"  /* immext(#0xfffbffc0), parse=01 */
        ".word 0x600237b0\n"  /* duplex: slot0=0x17b0 (invalid), slot1=0x0002 */
        : : : "memory");
    return 0;
}
