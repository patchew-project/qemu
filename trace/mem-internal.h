/*
 * Helper functions for guest memory tracing
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__MEM_INTERNAL_H
#define TRACE__MEM_INTERNAL_H

static inline TraceMemInfo trace_mem_get_info(TCGMemOp op, bool store)
{
    TraceMemInfo res_;
    uint8_t res = op;
    bool be = (op & MO_BSWAP) == MO_BE;

    /* remove untraced fields */
    res &= (1ULL << 4) - 1;
    /* make endianness absolute */
    res &= ~MO_BSWAP;
    if (be) {
        res |= 1ULL << 3;
    }
    /* add fields */
    if (store) {
        res |= 1ULL << 4;
    }

    res_.raw = res;
    return res_;
}

static inline TraceMemInfo trace_mem_build_info(
    TCGMemOp size, bool sign_extend, TCGMemOp endianness, bool store)
{
    TraceMemInfo res;
    res.size_shift = size;
    res.sign_extend = sign_extend;
    if (endianness == MO_BE) {
        res.endianness = 1;
    } else {
        res.endianness = 0;
    }
    res.store = store;
    return res;
}

#endif /* TRACE__MEM_INTERNAL_H */
