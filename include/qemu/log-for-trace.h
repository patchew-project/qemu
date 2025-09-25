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
 * qemu_log: report a log message
 * @fmt: the format string for the message
 * @...: the format string arguments
 *
 * This will emit a log message to the current output stream.
 *
 * The @fmt string should normally represent a complete line
 * of text, ending with a newline character.
 *
 * If intending to call this function multiple times to
 * incrementally construct a line of text, locking must
 * be used to ensure that output from different threads
 * is not interleaved.
 *
 * This is achieved by calling qemu_log_trylock() before
 * starting the log line; calling qemu_log() multiple
 * times with the last call having a newline at the end
 * of @fmt; finishing with a call to qemu_log_unlock().
 *
 * The FILE object returned by qemu_log_trylock() does
 * not need to be used for outputting text directly,
 * it is merely used to associate the lock.
 *
 *    FILE *f = qemu_log_trylock()
 *
 *    qemu_log("Something");
 *    qemu_log("Something");
 *    qemu_log("Something");
 *    qemu_log("The end\n");
 *
 *    qemu_log_unlock(f);
 *
 */
void G_GNUC_PRINTF(1, 2) qemu_log(const char *fmt, ...);

#endif
