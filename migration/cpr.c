/*
 * Copyright (c) 2021-2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/vfio/vfio-cpr.h"
#include "migration/cpr.h"
#include "migration/misc.h"
#include "migration/options.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "system/runstate.h"
#include "trace.h"

/*************************************************************************/
/* cpr state container for all information to be saved. */

CprState cpr_state;

/****************************************************************************/

typedef struct CprFdKey {
    char *name;
    unsigned int namelen;
    int id;
} CprFdKey;

typedef struct CprFdVal {
    int fd;
} CprFdVal;

#define VMSTATE_FDS_KEY                                                 \
{                                                                       \
    .name = "cpr-fd-key",                                               \
    .version_id = 1,                                                    \
    .minimum_version_id = 1,                                            \
    .fields = (const VMStateField[]) {                                  \
        VMSTATE_UINT32(namelen, CprFdKey),                              \
        VMSTATE_VBUFFER_ALLOC_UINT32(name, CprFdKey, 0, NULL, namelen), \
        VMSTATE_INT32(id, CprFdKey),                                    \
        VMSTATE_END_OF_LIST()                                           \
    }                                                                   \
}

#define VMSTATE_FDS_VAL                 \
{                                       \
    .name = "cpr-fd-value",             \
    .version_id = 1,                    \
    .minimum_version_id = 1,            \
    .fields = (const VMStateField[]) {  \
        VMSTATE_FD(fd, CprFdVal),       \
        VMSTATE_END_OF_LIST()           \
    }                                   \
}

static const VMStateDescription vmstate_fds_hashtable[2] = {
    VMSTATE_FDS_VAL,   /* value */
    VMSTATE_FDS_KEY   /* key */
};

static guint cpr_fd_key_hash(gconstpointer v)
{
    const CprFdKey *key = v;
    return g_str_hash(key->name) ^ key->id;
}

static gboolean cpr_fd_key_equal(gconstpointer a, gconstpointer b)
{
    const CprFdKey *key_a = a;
    const CprFdKey *key_b = b;
    return !strcmp(key_a->name, key_b->name) && key_a->id == key_b->id;
}

static void cpr_destroy_fd_key(gpointer data)
{
    CprFdKey *k = (CprFdKey *) data;
    g_free(k->name);
    g_free(k);
}

void cpr_state_init(void)
{
    CprState *s = &cpr_state;

    s->fds = g_hash_table_new_full(cpr_fd_key_hash, cpr_fd_key_equal,
                                   cpr_destroy_fd_key, g_free);
}

void cpr_save_fd(const char *name, int id, int fd)
{
    CprFdKey *key = g_new0(CprFdKey, 1);
    CprFdVal *val = g_new0(CprFdVal, 1);

    trace_cpr_save_fd(name, id, fd);
    key->name = g_strdup(name);
    key->namelen = strlen(name) + 1;
    key->id = id;
    val->fd = fd;
    g_hash_table_insert(cpr_state.fds, key, val);
}

static CprFdVal *find_fd(CprFdKey *key)
{
    return g_hash_table_lookup(cpr_state.fds, key);
}

void cpr_delete_fd(const char *name, int id)
{
    CprFdKey key = {
        .name = (char *)name,
        .id = id,
    };
    CprFdVal *elem = find_fd(&key);

    if (elem) {
        g_hash_table_remove(cpr_state.fds, &key);
    }

    trace_cpr_delete_fd(name, id);
}

int cpr_find_fd(const char *name, int id)
{
    CprFdKey key = {
        .name = (char *)name,
        .id = id,
    };
    CprFdVal *elem = find_fd(&key);
    int fd = elem ? elem->fd : -1;

    trace_cpr_find_fd(name, id, fd);
    return fd;
}

void cpr_resave_fd(const char *name, int id, int fd)
{
    CprFdKey key = {
        .name = (char *)name,
        .id = id,
    };
    CprFdVal *elem = find_fd(&key);
    int old_fd = elem ? elem->fd : -1;

    if (old_fd < 0) {
        cpr_save_fd(name, id, fd);
    } else if (old_fd != fd) {
        error_report("internal error: cpr fd '%s' id %d value %d "
                     "already saved with a different value %d",
                     name, id, fd, old_fd);
        g_assert_not_reached();
    }
}

int cpr_open_fd(const char *path, int flags, const char *name, int id,
                Error **errp)
{
    int fd = cpr_find_fd(name, id);

    if (fd < 0) {
        fd = qemu_open(path, flags, errp);
        if (fd >= 0) {
            cpr_save_fd(name, id, fd);
        }
    }
    return fd;
}

bool cpr_walk_fd(cpr_walk_fd_cb cb)
{
    GHashTableIter iter;
    CprFdVal *elem;

    g_hash_table_iter_init(&iter, cpr_state.fds);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&elem)) {
        g_assert(elem->fd >= 0);
        if (!cb(elem->fd)) {
            return false;
        }
    }
    return true;
}

/*************************************************************************/
static const VMStateDescription vmstate_cpr_state = {
    .name = CPR_STATE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_GHASH_V(fds, CprState, 1, vmstate_fds_hashtable,
                        CprFdKey, CprFdVal),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_cpr_vfio_devices,
        NULL
    }
};
/*************************************************************************/

static QEMUFile *cpr_state_file;

QIOChannel *cpr_state_ioc(void)
{
    return qemu_file_get_ioc(cpr_state_file);
}

static MigMode incoming_mode = MIG_MODE_NONE;

MigMode cpr_get_incoming_mode(void)
{
    return incoming_mode;
}

void cpr_set_incoming_mode(MigMode mode)
{
    incoming_mode = mode;
}

bool cpr_is_incoming(void)
{
    return incoming_mode != MIG_MODE_NONE;
}

bool cpr_state_save(MigrationChannel *channel, Error **errp)
{
    int ret;
    QEMUFile *f;
    MigMode mode = migrate_mode();

    trace_cpr_state_save(MigMode_str(mode));

    if (mode == MIG_MODE_CPR_TRANSFER) {
        g_assert(channel);
        f = cpr_transfer_output(channel, errp);
    } else if (mode == MIG_MODE_CPR_EXEC) {
        f = cpr_exec_output(errp);
    } else {
        return true;
    }
    if (!f) {
        return false;
    }

    qemu_put_be32(f, QEMU_CPR_FILE_MAGIC);
    qemu_put_be32(f, QEMU_CPR_FILE_VERSION);

    ret = vmstate_save_state(f, &vmstate_cpr_state, &cpr_state, 0, errp);
    if (ret) {
        qemu_fclose(f);
        return false;
    }

    if (migrate_mode() == MIG_MODE_CPR_EXEC) {
        if (!cpr_exec_persist_state(f, errp)) {
            qemu_fclose(f);
            return false;
        }
    }

    /*
     * Close the socket only partially so we can later detect when the other
     * end closes by getting a HUP event.
     */
    qemu_fflush(f);
    qio_channel_shutdown(qemu_file_get_ioc(f), QIO_CHANNEL_SHUTDOWN_WRITE,
                         NULL);
    cpr_state_file = f;
    return true;
}

int cpr_state_load(MigrationChannel *channel, Error **errp)
{
    int ret;
    uint32_t v;
    QEMUFile *f;
    MigMode mode = 0;

    if (cpr_exec_has_state()) {
        mode = MIG_MODE_CPR_EXEC;
        f = cpr_exec_input(errp);
        if (channel) {
            warn_report("ignoring cpr channel for migration mode cpr-exec");
        }
    } else if (channel) {
        mode = MIG_MODE_CPR_TRANSFER;
        cpr_set_incoming_mode(mode);
        f = cpr_transfer_input(channel, errp);
    } else {
        return 0;
    }
    if (!f) {
        return -1;
    }

    trace_cpr_state_load(MigMode_str(mode));
    cpr_set_incoming_mode(mode);

    v = qemu_get_be32(f);
    if (v != QEMU_CPR_FILE_MAGIC) {
        error_setg(errp, "Not a migration stream (bad magic %x)", v);
        qemu_fclose(f);
        return -EINVAL;
    }
    v = qemu_get_be32(f);
    if (v != QEMU_CPR_FILE_VERSION) {
        error_setg(errp, "Unsupported migration stream version %d", v);
        qemu_fclose(f);
        return -ENOTSUP;
    }

    ret = vmstate_load_state(f, &vmstate_cpr_state, &cpr_state, 1, errp);
    if (ret) {
        qemu_fclose(f);
        return ret;
    }

    if (migrate_mode() == MIG_MODE_CPR_EXEC) {
        /* Set cloexec to prevent fd leaks from fork until the next cpr-exec */
        cpr_exec_unpreserve_fds();
    }

    /*
     * Let the caller decide when to close the socket (and generate a HUP event
     * for the sending side).
     */
    cpr_state_file = f;

    return ret;
}

void cpr_state_close(void)
{
    if (cpr_state_file) {
        qemu_fclose(cpr_state_file);
        cpr_state_file = NULL;
    }
}

bool cpr_incoming_needed(void *opaque)
{
    MigMode mode = migrate_mode();
    return mode == MIG_MODE_CPR_TRANSFER || mode == MIG_MODE_CPR_EXEC;
}

/*
 * cpr_get_fd_param: find a descriptor and return its value.
 *
 * @name: CPR name for the descriptor
 * @fdname: An integer-valued string, or a name passed to a getfd command
 * @index: CPR index of the descriptor
 * @errp: returned error message
 *
 * If CPR is not being performed, then use @fdname to find the fd.
 * If CPR is being performed, then ignore @fdname, and look for @name
 * and @index in CPR state.
 *
 * On success returns the fd value, else returns -1.
 */
int cpr_get_fd_param(const char *name, const char *fdname, int index,
                     Error **errp)
{
    ERRP_GUARD();
    int fd;

    if (cpr_is_incoming()) {
        fd = cpr_find_fd(name, index);
        if (fd < 0) {
            error_setg(errp, "cannot find saved value for fd %s", fdname);
        }
    } else {
        fd = monitor_fd_param(monitor_cur(), fdname, errp);
        if (fd >= 0) {
            cpr_save_fd(name, index, fd);
        } else {
            error_prepend(errp, "Could not parse object fd %s:", fdname);
        }
    }
    return fd;
}
