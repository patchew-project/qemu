/*
 * Recorder-based trace backend
 *
 * Copyright Red Hat 2020
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef TRACE_RECORDER_H
#define TRACE_RECORDER_H

#include "qemu/osdep.h"

#ifdef CONFIG_TRACE_RECORDER

#include <recorder/recorder.h>

extern void recorder_trace_init(void);

#else

// Disable recorder macros
#define RECORDER(Name, Size, Description)
#define RECORDER_DEFINE(Name, Size, Description)
#define RECORDER_DECLARE(Name)
#define RECORD(Name, ...)
#define record(Name, ...)
#define recorder_trace_init()

#endif // CONFIG_TRACE_RECORDER

#endif // TRACE_RECORDER_H
