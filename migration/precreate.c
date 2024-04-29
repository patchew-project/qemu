/*
 * Copyright (c) 2022, 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/memfd.h"
#include "qapi/error.h"
#include "io/channel-file.h"
#include "migration/misc.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"

#define PRECREATE_STATE_NAME "QEMU_PRECREATE_STATE"

static QEMUFile *qemu_file_new_fd_input(int fd, const char *name)
{
    g_autoptr(QIOChannelFile) fioc = qio_channel_file_new_fd(fd);
    QIOChannel *ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(ioc, name);
    return qemu_file_new_input(ioc);
}

static QEMUFile *qemu_file_new_fd_output(int fd, const char *name)
{
    g_autoptr(QIOChannelFile) fioc = qio_channel_file_new_fd(fd);
    QIOChannel *ioc = QIO_CHANNEL(fioc);
    qio_channel_set_name(ioc, name);
    return qemu_file_new_output(ioc);
}

static int memfd_create_named(const char *name, Error **errp)
{
    int mfd;
    char val[16];

    mfd = memfd_create(name, 0);
    if (mfd < 0) {
        error_setg_errno(errp, errno, "memfd_create failed");
        return -1;
    }

    /* Remember mfd in environment for post-exec load */
    qemu_clear_cloexec(mfd);
    snprintf(val, sizeof(val), "%d", mfd);
    g_setenv(name, val, 1);

    return mfd;
}

static int memfd_find_named(const char *name, int *mfd_p, Error **errp)
{
    const char *val = g_getenv(name);

    if (!val) {
        *mfd_p = -1;
        return 0;       /* No memfd was created, not an error */
    }
    g_unsetenv(name);
    if (qemu_strtoi(val, NULL, 10, mfd_p)) {
        error_setg(errp, "Bad %s env value %s", PRECREATE_STATE_NAME, val);
        return -1;
    }
    lseek(*mfd_p, 0, SEEK_SET);
    return 0;
}

static void memfd_delete_named(const char *name)
{
    int mfd;
    const char *val = g_getenv(name);

    if (val) {
        g_unsetenv(name);
        if (!qemu_strtoi(val, NULL, 10, &mfd)) {
            close(mfd);
        }
    }
}

static QEMUFile *qemu_file_new_memfd_output(const char *name, Error **errp)
{
    int mfd = memfd_create_named(name, errp);

    if (mfd < 0) {
        return NULL;
    }

    return qemu_file_new_fd_output(mfd, name);
}

static QEMUFile *qemu_file_new_memfd_input(const char *name, Error **errp)
{
    int ret, mfd;

    ret = memfd_find_named(name, &mfd, errp);
    if (ret || mfd < 0) {
        return NULL;
    }

    return qemu_file_new_fd_input(mfd, name);
}

int migration_precreate_save(Error **errp)
{
    QEMUFile *f = qemu_file_new_memfd_output(PRECREATE_STATE_NAME, errp);

    if (!f) {
        return -1;
    } else if (qemu_savevm_precreate_save(f, errp)) {
        memfd_delete_named(PRECREATE_STATE_NAME);
        return -1;
    } else {
        /* Do not close f, as mfd must remain open. */
        return 0;
    }
}

void migration_precreate_unsave(void)
{
    memfd_delete_named(PRECREATE_STATE_NAME);
}

int migration_precreate_load(Error **errp)
{
    int ret;
    QEMUFile *f = qemu_file_new_memfd_input(PRECREATE_STATE_NAME, errp);

    if (!f) {
        return -1;
    }
    ret = qemu_savevm_precreate_load(f, errp);
    qemu_fclose(f);
    g_unsetenv(PRECREATE_STATE_NAME);
    return ret;
}
