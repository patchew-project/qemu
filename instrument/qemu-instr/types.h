/*
 * QEMU-specific types for instrumentation clients.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QI__TYPES_H
#define QI__TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>


/**
 * SECTION: types
 * @section_id: qi-types
 * @title: Common types
 *
 * Data of architecture-specific length is always passed as an #int64_t to
 * provide binary compatibility between the instrumentation library and QEMU,
 * regardless of the guest architecture being instrumented.
 */

/**
 * QITraceEvent:
 *
 * Opaque structure defining a tracing event.
 */
typedef struct QITraceEvent QITraceEvent;

/**
 * QITraceEventIter:
 *
 * Opaque structure defining a tracing event iterator.
 */
typedef struct QITraceEventIter QITraceEventIter;

/**
 * QICPU:
 *
 * Opaque guest CPU pointer.
 */
typedef struct QICPU_d *QICPU;

/**
 * QIMemInfo:
 * @size_shift: Memoy access size, interpreted as "1 << size_shift" bytes.
 * @sign_extend: Whether the access is sign-extended.
 * @endianness: Endianness type (0: little, 1: big).
 * @store: Whether it's a store operation.
 *
 * Memory access information.
 */
typedef struct QIMemInfo {
    union {
        struct {
            uint8_t size_shift : 2;
            bool    sign_extend: 1;
            uint8_t endianness : 1;
            bool    store      : 1;
        };
        uint8_t raw;
    };
} QIMemInfo;

/**
 * QITCGv_cpu:
 *
 * TCG register with QICPU.
 */
typedef struct QITCGv_cpu_d *QITCGv_cpu;

/**
 * QITCGv:
 *
 * TCG register with data of architecture-specific length.
 */
typedef struct QITCGv_d *QITCGv;

/**
 * QITCGv_i32:
 *
 * TCG register with 32-bit data.
 */
typedef struct QITCGv_i32_d *QITCGv_i32;

/**
 * QITCGv_i64:
 *
 * TCG register with 64-bit data.
 */
typedef struct QITCGv_i64_d *QITCGv_i64;

/*
 * QITCGv_ptr:
 *
 * TCG register with pointer of architecture-specific length.
 */
typedef struct QITCGv_ptr_d *QITCGv_ptr;


#include <qemu-instr/types.inc.h>

#ifdef __cplusplus
}
#endif

#endif  /* QI__TYPES_H */
