/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/queue.h"
#include "qemu/memfd.h"
#include "qapi/error.h"
#include "io/channel-file.h"
#include "migration/vmstate.h"
#include "migration/cpr-state.h"
#include "migration/migration.h"
#include "migration/misc.h"
#include "migration/qemu-file.h"
#include "trace.h"

/*************************************************************************/
/* cpr state container for all information to be saved. */

typedef QLIST_HEAD(CprNameList, CprName) CprNameList;

typedef struct CprState {
    MigMode mode;
    CprNameList fds;            /* list of CprFd */
    CprNameList memfd;          /* list of CprMemfd */
} CprState;

static CprState cpr_state = {
    .mode = MIG_MODE_NORMAL,
};

/*************************************************************************/
/* Generic list of names. */

typedef struct CprName {
    char *name;
    unsigned int namelen;
    int id;
    QLIST_ENTRY(CprName) next;
} CprName;

static const VMStateDescription vmstate_cpr_name = {
    .name = "cpr name",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(namelen, CprName),
        VMSTATE_VBUFFER_ALLOC_UINT32(name, CprName, 0, NULL, namelen),
        VMSTATE_INT32(id, CprName),
        VMSTATE_END_OF_LIST()
    }
};

static void
add_name(CprNameList *head, const char *name, int id, CprName *elem)
{
    elem->name = g_strdup(name);
    elem->namelen = strlen(name) + 1;
    elem->id = id;
    QLIST_INSERT_HEAD(head, elem, next);
}

static CprName *find_name(CprNameList *head, const char *name, int id)
{
    CprName *elem;

    QLIST_FOREACH(elem, head, next) {
        if (!strcmp(elem->name, name) && elem->id == id) {
            return elem;
        }
    }
    return NULL;
}

static void delete_name(CprNameList *head, const char *name, int id)
{
    CprName *elem = find_name(head, name, id);

    if (elem) {
        QLIST_REMOVE(elem, next);
        g_free(elem->name);
        g_free(elem);
    }
}

/****************************************************************************/
/* Lists of named things.  The first field of each entry must be a CprName. */

typedef struct CprFd {
    CprName name;               /* must be first */
    int fd;
} CprFd;

static const VMStateDescription vmstate_cpr_fd = {
    .name = "cpr fd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(name, CprFd, 1, vmstate_cpr_name, CprName),
        VMSTATE_INT32(fd, CprFd),
        VMSTATE_END_OF_LIST()
    }
};

#define CPR_FD(elem)        ((CprFd *)(elem))
#define CPR_FD_FD(elem)     (CPR_FD(elem)->fd)

void cpr_save_fd(const char *name, int id, int fd)
{
    CprFd *elem = g_new0(CprFd, 1);

    trace_cpr_save_fd(name, id, fd);
    elem->fd = fd;
    add_name(&cpr_state.fds, name, id, &elem->name);
}

void cpr_delete_fd(const char *name, int id)
{
    trace_cpr_delete_fd(name, id);
    delete_name(&cpr_state.fds, name, id);
}

int cpr_find_fd(const char *name, int id)
{
    CprName *elem = find_name(&cpr_state.fds, name, id);
    int fd = elem ? CPR_FD_FD(elem) : -1;

    if (fd >= 0) {
        /* Set cloexec to prevent fd leaks from fork until the next cpr-exec */
        qemu_set_cloexec(fd);
    }

    trace_cpr_find_fd(name, id, fd);
    return fd;
}

int cpr_walk_fd(cpr_walk_fd_cb cb, void *opaque)
{
    CprName *elem;

    QLIST_FOREACH(elem, &cpr_state.fds, next) {
        if (cb(elem->name, elem->id, CPR_FD_FD(elem), opaque)) {
            return 1;
        }
    }
    return 0;
}

void cpr_resave_fd(const char *name, int id, int fd)
{
    CprName *elem = find_name(&cpr_state.fds, name, id);
    int old_fd = elem ? CPR_FD_FD(elem) : -1;

    if (old_fd < 0) {
        cpr_save_fd(name, id, fd);
    } else if (old_fd != fd) {
        error_setg(&error_fatal,
                   "internal error: cpr fd '%s' id %d value %d "
                   "already saved with a different value %d",
                   name, id, fd, old_fd);
    }
}

/*************************************************************************/
/* A memfd ram block. */

typedef struct CprMemfd {
    CprName name;               /* must be first */
    size_t len;
    size_t maxlen;
    uint64_t align;
} CprMemfd;

static const VMStateDescription vmstate_cpr_memfd = {
    .name = "cpr memfd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(name, CprMemfd, 1, vmstate_cpr_name, CprName),
        VMSTATE_UINT64(len, CprMemfd),
        VMSTATE_UINT64(maxlen, CprMemfd),
        VMSTATE_UINT64(align, CprMemfd),
        VMSTATE_END_OF_LIST()
    }
};

#define CPR_MEMFD(elem)        ((CprMemfd *)(elem))
#define CPR_MEMFD_LEN(elem)    (CPR_MEMFD(elem)->len)
#define CPR_MEMFD_MAXLEN(elem) (CPR_MEMFD(elem)->maxlen)
#define CPR_MEMFD_ALIGN(elem)  (CPR_MEMFD(elem)->align)

void cpr_save_memfd(const char *name, int fd, size_t len, size_t maxlen,
                    uint64_t align)
{
    CprMemfd *elem = g_new0(CprMemfd, 1);

    trace_cpr_save_memfd(name, len, maxlen, align);
    elem->len = len;
    elem->maxlen = maxlen;
    elem->align = align;
    add_name(&cpr_state.memfd, name, 0, &elem->name);
    cpr_save_fd(name, 0, fd);
}

void cpr_delete_memfd(const char *name)
{
    trace_cpr_delete_memfd(name);
    delete_name(&cpr_state.memfd, name, 0);
    cpr_delete_fd(name, 0);
}

int cpr_find_memfd(const char *name, size_t *lenp, size_t *maxlenp,
                   uint64_t *alignp)
{
    int fd = cpr_find_fd(name, 0);
    CprName *elem = find_name(&cpr_state.memfd, name, 0);

    if (elem) {
        *lenp = CPR_MEMFD_LEN(elem);
        *maxlenp = CPR_MEMFD_MAXLEN(elem);
        *alignp = CPR_MEMFD_ALIGN(elem);
    } else {
        *lenp = 0;
        *maxlenp = 0;
        *alignp = 0;
    }

    trace_cpr_find_memfd(name, *lenp, *maxlenp, *alignp);
    return fd;
}

/*************************************************************************/
/* cpr state container interface and implementation. */

#define CPR_STATE_NAME "QEMU_CPR_STATE"

static const VMStateDescription vmstate_cpr_state = {
    .name = CPR_STATE_NAME,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(mode, CprState),
        VMSTATE_QLIST_V(fds, CprState, 1, vmstate_cpr_fd, CprFd, name.next),
        VMSTATE_QLIST_V(memfd, CprState, 1, vmstate_cpr_memfd, CprMemfd,
                        name.next),
        VMSTATE_END_OF_LIST()
    }
};

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

int cpr_state_save(Error **errp)
{
    int ret, mfd;
    QEMUFile *f;
    char val[16];

    mfd = memfd_create(CPR_STATE_NAME, 0);
    if (mfd < 0) {
        error_setg_errno(errp, errno, "memfd_create failed");
        return -1;
    }

    cpr_state.mode = migrate_mode();
    qemu_clear_cloexec(mfd);

    f = qemu_file_new_fd_output(mfd, CPR_STATE_NAME);
    ret = vmstate_save_state(f, &vmstate_cpr_state, &cpr_state, 0);
    if (ret) {
        error_setg(errp, "vmstate_save_state error %d", ret);
        goto error;
    }

    /* Do not close f, as mfd must remain open. */
    qemu_fflush(f);
    lseek(mfd, 0, SEEK_SET);

    /* Remember mfd for post-exec cpr_state_load */
    snprintf(val, sizeof(val), "%d", mfd);
    g_setenv(CPR_STATE_NAME, val, 1);

    return 0;

error:
    close(mfd);
    cpr_state.mode = MIG_MODE_NORMAL;
    return ret;
}

void cpr_state_unsave(void)
{
    int mfd;
    const char *val = g_getenv(CPR_STATE_NAME);

    if (val) {
        g_unsetenv(CPR_STATE_NAME);
        if (!qemu_strtoi(val, NULL, 10, &mfd)) {
            close(mfd);
        }
    }
}

int cpr_state_load(Error **errp)
{
    int ret, mfd;
    QEMUFile *f;
    const char *val = g_getenv(CPR_STATE_NAME);

    if (!val) {
        return 0;
    }
    g_unsetenv(CPR_STATE_NAME);
    if (qemu_strtoi(val, NULL, 10, &mfd)) {
        error_setg(errp, "Bad %s env value %s", CPR_STATE_NAME, val);
        return 1;
    }
    f = qemu_file_new_fd_input(mfd, CPR_STATE_NAME);
    ret = vmstate_load_state(f, &vmstate_cpr_state, &cpr_state, 1);
    qemu_fclose(f);

    if (!ret) {
        migrate_get_current()->parameters.mode = cpr_state.mode;
    } else {
        error_setg(errp, "vmstate_load_state error %d", ret);
    }

    return ret;
}

void cpr_state_print(void)
{
    CprName *elem;

    printf("cpr_state:\n");
    printf("- mode = %d\n", cpr_state.mode);
    QLIST_FOREACH(elem, &cpr_state.fds, next) {
        printf("- %s %d : fd=%d\n", elem->name, elem->id, CPR_FD_FD(elem));
    }
    QLIST_FOREACH(elem, &cpr_state.memfd, next) {
        printf("- %s : len=%lu, maxlen=%lu, align=%lu\n", elem->name,
               CPR_MEMFD_LEN(elem), CPR_MEMFD_MAXLEN(elem),
               CPR_MEMFD_ALIGN(elem));
    }
}
