/*
 * Linux perf perf-<pid>.map and jit-<pid>.dump integration.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_PERF_H
#define ACCEL_TCG_PERF_H

#include <stddef.h>
#include <stdint.h>

/* Start writing perf-<pid>.map. */
void perf_enable_perfmap(void);

/* Start writing jit-<pid>.dump. */
void perf_enable_jitdump(void);

/* Add information about TCG prologue to profiler maps. */
void perf_report_prologue(const void *start, size_t size);

/* Add information about JITted guest code to profiler maps. */
void perf_report_code(const void *start, size_t size, int icount, uint64_t pc);

/* Stop writing perf-<pid>.map and/or jit-<pid>.dump. */
void perf_exit(void);

#endif
