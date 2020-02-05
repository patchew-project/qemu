/*
 * QEMU file-based self-fence mechanism
 *
 * Copyright (c) 2019 Nutanix Inc. All rights reserved.
 *
 * Authors:
 *    Felipe Franciosi <felipe@nutanix.com>
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "qemu/filemonitor.h"
#include "qemu/timer.h"

#include <time.h>

#define TYPE_FILE_FENCE "file-fence"

typedef struct FileFence {
    Object parent_obj;

    gchar *dir;
    gchar *file;
    uint32_t qtimeout;
    uint32_t ktimeout;
    int signal;

    timer_t ktimer;
    QEMUTimer *qtimer;

    QFileMonitor *fm;
    uint64_t id;
} FileFence;

#define FILE_FENCE(obj) \
    OBJECT_CHECK(FileFence, (obj), TYPE_FILE_FENCE)

static void
timer_update(FileFence *ff)
{
    struct itimerspec its = {
        .it_value.tv_sec = ff->ktimeout,
    };
    int err;

    if (ff->qtimeout) {
        timer_mod(ff->qtimer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
                              ff->qtimeout * 1000);
    }

    if (ff->ktimeout) {
        err = timer_settime(ff->ktimer, 0, &its, NULL);
        g_assert(err == 0);
    }
}

static void
file_fence_abort_cb(void *opaque)
{
    FileFence *ff = opaque;
    error_printf("Fencing after %u seconds on '%s'\n",
                 ff->qtimeout, g_strconcat(ff->dir, "/", ff->file, NULL));
    abort();
}

static void
file_fence_watch_cb(int64_t id, QFileMonitorEvent ev, const char *file,
                    void *opaque)
{
    FileFence *ff = opaque;

    if (ev != QFILE_MONITOR_EVENT_ATTRIBUTES) {
        return;
    }

    g_assert(g_str_equal(file, ff->file));

    timer_update(ff);
}

static void
ktimer_tear(FileFence *ff)
{
    int err;

    if (ff->ktimer) {
        err = timer_delete(ff->ktimer);
        g_assert(err == 0);
        ff->ktimer = NULL;
    }
}

static gboolean
ktimer_setup(FileFence *ff, Error **errp)
{
    int err;

    struct sigevent sev = {
        .sigev_notify = SIGEV_SIGNAL,
        .sigev_signo = ff->signal ? ff->signal : SIGKILL,
    };

    if (ff->ktimeout == 0) {
        return TRUE;
    }

    err = timer_create(CLOCK_MONOTONIC, &sev, &ff->ktimer);
    if (err == -1) {
        error_setg(errp, "Error creating kernel timer: %m");
        return FALSE;
    }

    return TRUE;
}

static void
qtimer_tear(FileFence *ff)
{
    if (ff->qtimer) {
        timer_del(ff->qtimer);
        timer_free(ff->qtimer);
    }
    ff->qtimer = NULL;
}

static gboolean
qtimer_setup(FileFence *ff, Error **errp)
{
    QEMUTimer *qtimer;

    if (ff->qtimeout == 0) {
        return TRUE;
    }

    qtimer = timer_new_ms(QEMU_CLOCK_REALTIME, file_fence_abort_cb, ff);
    if (qtimer == NULL) {
        error_setg(errp, "Error creating Qemu timer");
        return FALSE;
    }

    ff->qtimer = qtimer;

    return TRUE;
}

static void
watch_tear(FileFence *ff)
{
    if (ff->fm) {
        qemu_file_monitor_remove_watch(ff->fm, ff->dir, ff->id);
        qemu_file_monitor_free(ff->fm);
        ff->fm = NULL;
        ff->id = 0;
    }
}

static gboolean
watch_setup(FileFence *ff, Error **errp)
{
    QFileMonitor *fm;
    int64_t id;

    fm = qemu_file_monitor_new(errp);
    if (!fm) {
        return FALSE;
    }

    id = qemu_file_monitor_add_watch(fm, ff->dir, ff->file,
                                     file_fence_watch_cb, ff, errp);
    if (id < 0) {
        qemu_file_monitor_free(fm);
        return FALSE;
    }

    ff->fm = fm;
    ff->id = id;

    return TRUE;
}

static void
file_fence_complete(UserCreatable *obj, Error **errp)
{
    FileFence *ff = FILE_FENCE(obj);

    if (ff->dir == NULL) {
        error_setg(errp, "A 'file' must be set");
        return;
    }

    if (ff->signal != 0 && ff->ktimeout == 0) {
        error_setg(errp, "Using 'signal' requires 'ktimeout' to be set");
        return;
    }

    if (ff->ktimeout == 0 && ff->qtimeout == 0) {
        error_setg(errp, "One or both of 'ktimeout' or 'qtimeout' must be set");
        return;
    }

    if (ff->qtimeout >= ff->ktimeout && ff->ktimeout != 0) {
        error_setg(errp, "Using 'qtimeout' >= 'ktimeout' doesn't make sense");
        return;
    }

    if (!watch_setup(ff, errp) ||
        !qtimer_setup(ff, errp) ||
        !ktimer_setup(ff, errp)) {
        return;
    }

    timer_update(ff);

    return;
}

static void
file_fence_set_signal(Object *obj, const char *value, Error **errp)
{
    FileFence *ff = FILE_FENCE(obj);

    if (ff->signal) {
        error_setg(errp, "Signal property already set");
        return;
    }

    if (value == NULL) {
        goto err;
    }

    if (g_ascii_strcasecmp(value, "QUIT") == 0) {
        ff->signal = SIGQUIT;
        return;
    }

    if (g_ascii_strcasecmp(value, "KILL") == 0) {
        ff->signal = SIGKILL;
        return;
    }

err:
    error_setg(errp, "Invalid signal. Must be 'quit' or 'kill'");
}

static char *
file_fence_get_signal(Object *obj, Error **errp)
{
    FileFence *ff = FILE_FENCE(obj);

    switch (ff->signal) {
    case SIGKILL:
        return g_strdup("kill");
    case SIGQUIT:
        return g_strdup("quit");
    }

    /* Unreachable */
    abort();
}

static void
file_fence_set_file(Object *obj, const char *value, Error **errp)
{
    FileFence *ff = FILE_FENCE(obj);
    g_autofree gchar *dir = NULL, *file = NULL;

    if (ff->dir) {
        error_setg(errp, "File property already set");
        return;
    }

    dir = g_path_get_dirname(value);
    if (g_str_equal(dir, ".")) {
        error_setg(errp, "Path for file-fence must be absolute");
        return;
    }

    file = g_path_get_basename(value);
    if (g_str_equal(file, ".")) {
        error_setg(errp, "Path for file-fence must be a file");
        return;
    }

    ff->dir = g_steal_pointer(&dir);
    ff->file = g_steal_pointer(&file);
}

static char *
file_fence_get_file(Object *obj, Error **errp)
{
    FileFence *ff = FILE_FENCE(obj);

    if (ff->file) {
        return g_build_filename(ff->dir, ff->file, NULL);
    }

    return NULL;
}

static void
file_fence_instance_finalize(Object *obj)
{
    FileFence *ff = FILE_FENCE(obj);

    ktimer_tear(ff);
    qtimer_tear(ff);
    watch_tear(ff);

    g_free(ff->file);
    g_free(ff->dir);
}

static void
file_fence_instance_init(Object *obj)
{
    FileFence *ff = FILE_FENCE(obj);

    object_property_add_str(obj, "file",
                            file_fence_get_file,
                            file_fence_set_file,
                            &error_abort);
    object_property_add_str(obj, "signal",
                            file_fence_get_signal,
                            file_fence_set_signal,
                            &error_abort);
    object_property_add_uint32_ptr(obj, "qtimeout", &ff->qtimeout,
                                   OBJ_PROP_FLAG_READWRITE, &error_abort);
    object_property_add_uint32_ptr(obj, "ktimeout", &ff->ktimeout,
                                   OBJ_PROP_FLAG_READWRITE, &error_abort);
}

static void
file_fence_class_init(ObjectClass *klass, void *class_data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(klass);
    ucc->complete = file_fence_complete;
}

static const TypeInfo file_fence_info = {
    .name = TYPE_FILE_FENCE,
    .parent = TYPE_OBJECT,
    .class_init = file_fence_class_init,
    .instance_size = sizeof(FileFence),
    .instance_init = file_fence_instance_init,
    .instance_finalize = file_fence_instance_finalize,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
register_types(void)
{
    type_register_static(&file_fence_info);
}

type_init(register_types);
