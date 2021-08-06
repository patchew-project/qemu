#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/queue.h"
#include "qemu/memfd.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "migration/cpr.h"
#include "migration/qemu-file.h"
#include "migration/qemu-file-channel.h"
#include "trace.h"

/*************************************************************************/
/* cpr state container for all information to be saved. */

typedef QLIST_HEAD(CprNameList, CprName) CprNameList;

typedef struct CprState {
    CprMode mode;
    CprNameList fds;            /* list of CprFd */
} CprState;

static CprState cpr_state;

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
        VMSTATE_END_OF_LIST()
    }
};

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
    qemu_clear_cloexec(mfd);
    f = qemu_fd_open(mfd, true, CPR_STATE_NAME);

    ret = vmstate_save_state(f, &vmstate_cpr_state, &cpr_state, 0);
    if (ret) {
        error_setg(errp, "vmstate_save_state error %d", ret);
        return ret;
    }

    /* Do not close f, as mfd must remain open. */
    qemu_fflush(f);
    lseek(mfd, 0, SEEK_SET);

    /* Remember mfd for post-exec cpr_state_load */
    snprintf(val, sizeof(val), "%d", mfd);
    g_setenv(CPR_STATE_NAME, val, 1);

    return 0;
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
    f = qemu_fd_open(mfd, false, CPR_STATE_NAME);
    ret = vmstate_load_state(f, &vmstate_cpr_state, &cpr_state, 1);
    qemu_fclose(f);
    return ret;
}

CprMode cpr_state_mode(void)
{
    return cpr_state.mode;
}

void cpr_state_print(void)
{
    CprName *elem;

    QLIST_FOREACH(elem, &cpr_state.fds, next) {
        printf("%s %d : %d\n", elem->name, elem->id, CPR_FD_FD(elem));
    }
}
