/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_VALUE64_H
#define QEMU_VALUE64_H

#ifdef __aarch64__
#include <arm_acle.h>
#endif

/*
 * An stream of 64 bytes; no endianness implied.
 * The members are perforce host byte ordering.
 */
typedef union {
    uint64_t l[8];
#ifdef __aarch64__
    data512_t arm;
#endif
} Value64;

#endif /* QEMU_VALUE64_H */
