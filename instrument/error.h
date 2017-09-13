/*
 * Helpers for controlling errors in instrumentation libraries.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef INSTRUMENT_ERROR_H
#define INSTRUMENT_ERROR_H

#include "qemu/osdep.h"
#include "qemu/error-report.h"


#define _ERROR(msg, args...)                            \
    do {                                                \
        error_report("%s:" msg, __func__, ##args);      \
    } while (0)

#define ERROR_IF(cond, msg, args...) \
    if (unlikely(cond)) {            \
        _ERROR(msg, ##args);         \
        return;                      \
    }

#define ERROR_IF_RET(cond, ret, msg, args...)   \
    if (unlikely(cond)) {                       \
        _ERROR(msg, ##args);                    \
        return ret;                             \
    }                                           \

#endif  /* INSTRUMENT_ERROR_H */
