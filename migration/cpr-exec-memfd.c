// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CPR exec migration via memfd
 *
 * Copyright (c) 2026 Oracle and/or its affiliates.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "channel.h"
#include "cpr-exec-memfd.h"
#include "memfd.h"

#define CPR_EXEC_MEMFD_NAME "qemu-cpr-exec-migration"
#define CPR_EXEC_MEMFD_ENV "QEMU_CPR_EXEC_MIGRATION_FD"

static int cpr_exec_memfd_fd = -1;

static int cpr_exec_memfd_claim_fd(Error **errp)
{
    const char *val = g_getenv(CPR_EXEC_MEMFD_ENV);
    int fd;

    if (!val) {
        error_setg(errp, "missing %s environment variable",
                   CPR_EXEC_MEMFD_ENV);
        return -1;
    }

    if (qemu_strtoi(val, NULL, 10, &fd) < 0) {
        error_setg(errp, "bad %s value %s", CPR_EXEC_MEMFD_ENV, val);
        return -1;
    }

    g_unsetenv(CPR_EXEC_MEMFD_ENV);
    return fd;
}

QIOChannel *cpr_exec_memfd_connect_outgoing(Error **errp)
{
    return memfd_create_outgoing(CPR_EXEC_MEMFD_NAME, &cpr_exec_memfd_fd,
                                 errp);
}

void cpr_exec_memfd_connect_incoming(Error **errp)
{
    g_autoptr(QIOChannel) ioc = NULL;
    int fd = cpr_exec_memfd_claim_fd(errp);

    if (fd < 0) {
        return;
    }

    ioc = memfd_open_incoming(fd, CPR_EXEC_MEMFD_NAME, errp);
    if (!ioc) {
        return;
    }

    migration_channel_process_incoming(ioc);
}

bool cpr_exec_memfd_preserve_fd(Error **errp)
{
    char val[16];

    if (cpr_exec_memfd_fd < 0) {
        return true;
    }

    qemu_clear_cloexec(cpr_exec_memfd_fd);
    snprintf(val, sizeof(val), "%d", cpr_exec_memfd_fd);
    if (!g_setenv(CPR_EXEC_MEMFD_ENV, val, 1)) {
        error_setg(errp, "Setting env %s = %s failed",
                   CPR_EXEC_MEMFD_ENV, val);
        return false;
    }

    return true;
}

void cpr_exec_memfd_cleanup(void)
{
    g_unsetenv(CPR_EXEC_MEMFD_ENV);

    if (cpr_exec_memfd_fd >= 0) {
        close(cpr_exec_memfd_fd);
        cpr_exec_memfd_fd = -1;
    }
}
