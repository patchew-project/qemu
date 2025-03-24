/*
 * GDB Common Register Helpers
 *
 * Copyright (c) 2025 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef GDB_REGISTERS_H
#define GDB_REGISTERS_H

#include "exec/memop.h"

/**
 * gdb_get_register_value() - get register value for gdb
 * mo: size and endian MemOp
 * buf: GByteArray to store in target order
 * val: pointer to value in host order
 *
 * This replaces the previous legacy read functions with a single
 * function to handle all sizes. Passing @mo allows the target mode to
 * be taken into account and avoids using hard coded tswap() macros.
 *
 * There are wrapper functions for the common sizes you can use to
 * keep type checking.
 *
 * Returns the number of bytes written to the array.
 */
int gdb_get_register_value(MemOp op, GByteArray *buf, void *val);

/**
 * gdb_get_reg32_value() - type checked wrapper for gdb_get_register_value()
 * mo: size and endian MemOp
 * buf: GByteArray to store in target order
 * val: pointer to uint32_t value in host order
 */
static inline int gdb_get_reg32_value(MemOp op, GByteArray *buf, uint32_t *val) {
    g_assert((op & MO_SIZE) == MO_32);
    return gdb_get_register_value(op, buf, val);
}

/**
 * gdb_get_reg64_value() - type checked wrapper for gdb_get_register_value()
 * mo: size and endian MemOp
 * buf: GByteArray to store in target order
 * val: pointer to uint32_t value in host order
 */
static inline int gdb_get_reg64_value(MemOp op, GByteArray *buf, uint64_t *val) {
    g_assert((op & MO_SIZE) == MO_64);
    return gdb_get_register_value(op, buf, val);
}

#endif /* GDB_REGISTERS_H */


