/* log-for-trace.h: logging basics required by the trace.h generated
 * by the log trace backend.
 *
 * This should not be included directly by any .c file: if you
 * need to use the logging functions include "qemu/log.h".
 *
 * The purpose of splitting these parts out into their own header
 * is to catch the easy mistake where a .c file includes trace.h
 * but forgets to include qemu/log.h. Without this split, that
 * would result in the .c file compiling fine when the default
 * trace backend is in use but failing to compile with any other
 * backend.
 *
 * This code is licensed under the GNU General Public License,
 * version 2 or (at your option) any later version.
 */

#ifndef QEMU_LOG_FOR_TRACE_H
#define QEMU_LOG_FOR_TRACE_H

/* Private global variable, don't use */
extern int qemu_loglevel;

#define LOG_TRACE          (1 << 15)

/* Returns true if a bit is set in the current loglevel mask */
static inline bool qemu_loglevel_mask(int mask)
{
    return (qemu_loglevel & mask) != 0;
}

/**
 * qemu_log: main logging function
 *
 * Most users shouldn't be calling qemu_log unconditionally as it adds
 * noise to logging output. Either use qemu_log_mask() or wrap
 * successive log calls a qemu_loglevel_mask() check and
 * qemu_log_lock/unlock(). The tracing infrastructure does similar wrapping.
 */
int GCC_FMT_ATTR(1, 2) qemu_log(const char *fmt, ...);

#endif
