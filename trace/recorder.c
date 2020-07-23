/*
 * Recorder-based trace backend
 *
 * Copyright Red Hat 2020
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "trace/recorder.h"

RECORDER_CONSTRUCTOR
void recorder_trace_init(void)
{
    const char *traces = getenv("RECORDER_TRACES");
    recorder_trace_set(traces);

    /*
     * Allow a dump in case we receive some unhandled signal
     * For example, send USR2 to a hung process to get a dump
     */
    if (traces) {
        recorder_dump_on_common_signals(0, 0);
    }
}
