/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_VALUE64_H
#define QEMU_VALUE64_H

/*
 * An stream of 64 bytes; no endianness implied.
 * The members are perforce host byte ordering.
 */
typedef union {
    uint64_t l[8];
} Value64;

#endif /* QEMU_VALUE64_H */
