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

/**
 * SECTION: types
 * @section_id: qi-types
 * @title: Common types
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


#include <qemu-instr/types.inc.h>

#ifdef __cplusplus
}
#endif

#endif  /* QI__TYPES_H */
