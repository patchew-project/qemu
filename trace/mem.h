/*
 * Helper functions for guest memory tracing
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__MEM_H
#define TRACE__MEM_H

#include "tcg/tcg.h"

/**
 * TraceMemInfo:
 * @size_shift: Memoy access size, interpreted as "1 << size_shift" bytes.
 * @sign_extend: Whether the access is sign-extended.
 * @endianness: Endinness type (0: little, 1: big).
 * @store: Whether it's a store operation.
 *
 * Memory access information.
 *
 * NOTE: Keep in sync with QIMemInfo.
 */
typedef struct TraceMemInfo {
    union {
        struct {
            uint8_t size_shift : 2;
            bool    sign_extend: 1;
            uint8_t endianness : 1;
            bool    store      : 1;
        };
        uint8_t raw;
    };
} TraceMemInfo;


/**
 * trace_mem_get_info:
 *
 * Return a value for the 'info' argument in guest memory access traces.
 */
static TraceMemInfo trace_mem_get_info(TCGMemOp op, bool store);

/**
 * trace_mem_build_info:
 *
 * Return a value for the 'info' argument in guest memory access traces.
 */
static TraceMemInfo trace_mem_build_info(TCGMemOp size, bool sign_extend,
                                         TCGMemOp endianness, bool store);


#include "trace/mem-internal.h"

#endif /* TRACE__MEM_H */
