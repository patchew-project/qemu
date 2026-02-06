/*
 * Test that duplex encodings with duplicate destination registers are rejected.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The encoding 0x00000000 decodes as a duplex with parse bits [15:14] = 0b00:
 *   slot1: SL1_loadri_io R0 = memw(R0+#0x0)
 *   slot0: SL1_loadri_io R0 = memw(R0+#0x0)
 *
 * Both sub-instructions write R0, which is an invalid packet (duplicate
 * destination register).  This should raise SIGILL.
 */

int main()
{
    asm volatile(
        ".word 0x00000000\n"
        : : : "r0", "memory");
    return 0;
}
