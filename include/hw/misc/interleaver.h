/*
 * QEMU Interleaver device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_INTERLEAVER_H
#define HW_MISC_INTERLEAVER_H

/*
 * Example of 32x16 interleaver accesses (32-bit bus, 2x 16-bit banks):
 *
 * Each interleaved 32-bit access on the bus results in contiguous 16-bit
 * access on each banked device:
 *
 *                      ____________________________________________________
 *   Bus accesses       |        1st 32-bit       |        2nd 32-bit       |
 *                      -----------------------------------------------------
 *                             |            |            |            |
 *                             v            |            v            |
 *                      ______________      |     ______________      |
 *   1st bank accesses  | 1st 16-bit |      |     | 2nd 16-bit |      |
 *                      --------------      |     --------------      |
 *                                          v                         v
 *                                   ______________            ______________
 *   2nd bank accesses               | 1st 16-bit |            | 2nd 16-bit |
 *                                   --------------            --------------
 */

#define TYPE_INTERLEAVER_16X8_DEVICE    "interleaver-16x8-device"
#define TYPE_INTERLEAVER_32X8_DEVICE    "interleaver-32x8-device"
#define TYPE_INTERLEAVER_32X16_DEVICE   "interleaver-32x16-device"
#define TYPE_INTERLEAVER_64X8_DEVICE    "interleaver-64x8-device"
#define TYPE_INTERLEAVER_64X16_DEVICE   "interleaver-64x16-device"
#define TYPE_INTERLEAVER_64X32_DEVICE   "interleaver-64x32-device"

#endif

