/*
 * Backtrace Functions
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _BACKTRACE_H_
#define _BACKTRACE_H_

#ifdef CONFIG_BACKTRACE
/**
 * qemu_backtrace() - return a backtrace of current thread
 * max: maximum number of lines of backtrace
 *
 * Return an allocated GString containing the backtrace of the current
 * thread. Caller frees the GString once done.
 */
GString *qemu_backtrace(int max);
#else
static inline GString *qemu_backtrace(int max)
{
    return NULL;
}
#endif

#endif /* _BACKTRACE_H_ */
