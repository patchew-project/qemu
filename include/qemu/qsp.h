/*
 * qsp.c - QEMU Synchronization Profiler
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * Note: this header file can *only* be included from thread.h.
 */
#ifndef QEMU_QSP_H
#define QEMU_QSP_H

#include "qemu/fprintf-fn.h"

#ifdef CONFIG_SYNC_PROFILER

void qsp_mutex_lock(QemuMutex *mutex, const char *file, unsigned line);
int qsp_mutex_trylock(QemuMutex *mutex, const char *file, unsigned line);

void qsp_bql_mutex_lock(QemuMutex *mutex, const char *file, unsigned line);

void qsp_rec_mutex_lock(QemuRecMutex *mutex, const char *file, unsigned line);
int qsp_rec_mutex_trylock(QemuRecMutex *mutex, const char *file, unsigned line);

void qsp_cond_wait(QemuCond *cond, QemuMutex *mutex, const char *file,
                   unsigned line);

void qsp_report(FILE *f, fprintf_function cpu_fprintf, size_t max);

#else /* !CONF_SYNC_PROFILER */

static inline void qsp_report(FILE *f, fprintf_function cpu_fprintf, size_t max)
{
    cpu_fprintf(f, "[Sync profiler not compiled]\n");
}

#endif /* !CONF_SYNC_PROFILER */

#endif /* QEMU_QSP_H */
