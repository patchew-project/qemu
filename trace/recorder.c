/*
 * Recorder-based trace backend
 *
 * Copyright Red Hat 2020
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "trace/recorder.h"

RECORDER_CONSTRUCTOR
void recorder_trace_init(void)
{
    recorder_trace_set(getenv("RECORDER_TRACES"));

    // Allow a dump in case we receive some unhandled signal
    // For example, send USR2 to a hung process to get a dump
    if (getenv("RECORDER_TRACES"))
        recorder_dump_on_common_signals(0,0);
}
