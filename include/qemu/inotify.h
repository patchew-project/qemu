/*
 * QEMU inotify helper
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QEMU_INOTIFY_H
#define QEMU_INOTIFY_H

#include "qemu-common.h"
#ifdef CONFIG_INOTIFY1
#include <sys/inotify.h>
#endif


typedef struct QInotify QInotify;

typedef void (*QInotifyHandler)(int wd,
                                   uint32_t mask,
                                   const char *name,
                                   void *opaque);

QInotify *qemu_inotify_new(QInotifyHandler cb,
                           void *opaque,
                           GDestroyNotify ffcb,
                           Error **errp);

void qemu_inotify_free(QInotify *in);

int qemu_inotify_add_watch(QInotify *in,
                           const char *path,
                           uint32_t mask,
                           Error **errp);

#endif /* QEMU_INOTIFY_H */
