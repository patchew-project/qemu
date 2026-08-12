// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU live migration via memfd
 *
 * Copyright (c) 2026 Oracle and/or its affiliates.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/memfd.h"
#include "memfd.h"

QIOChannel *memfd_create_outgoing(const char *name, int *dup_fdp,
                                  Error **errp)
{
    QIOChannelFile *fioc;
    QIOChannel *ioc;
    int fd;

#ifdef CONFIG_LINUX
    fd = qemu_memfd_create(name, 0, false, 0, 0, errp);
#else
    fd = -1;
    error_setg(errp, "memfd migration is only supported on Linux");
#endif

    if (fd < 0) {
        return NULL;
    }

    if (dup_fdp) {
        int dup_fd = dup(fd);

        if (dup_fd < 0) {
            error_setg_errno(errp, errno,
                             "failed to duplicate memfd migration fd");
            close(fd);
            return NULL;
        }

        qemu_set_cloexec(dup_fd);
        *dup_fdp = dup_fd;
    }

    fioc = qio_channel_file_new_fd(fd);
    ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(ioc, name);
    return ioc;
}

QIOChannel *memfd_open_incoming(int fd, const char *name, Error **errp)
{
    QIOChannelFile *fioc;
    QIOChannel *ioc;

    if (lseek(fd, 0, SEEK_SET) < 0) {
        error_setg_errno(errp, errno, "failed to rewind memfd migration fd");
        close(fd);
        return NULL;
    }

    fioc = qio_channel_file_new_fd(fd);
    ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(ioc, name);
    return ioc;
}
