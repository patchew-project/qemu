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
 * Returns the number of bytes written to the array.
 */
int gdb_get_register_value(MemOp op, GByteArray *buf, uint8_t *val);

#endif /* GDB_REGISTERS_H */


