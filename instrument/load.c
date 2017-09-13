/*
 * Interface for (un)loading instrumentation libraries.
 *
 * Copyright (C) 2012-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include <dlfcn.h>
#include "exec/cpu-common.h"
#include "instrument/control.h"
#include "instrument/events.h"
#include "instrument/load.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"


typedef struct InstrHandle {
    char *id;
    void *dlhandle;
    QLIST_ENTRY(InstrHandle) list;
} InstrHandle;


static unsigned int handle_auto_id;
static QLIST_HEAD(, InstrHandle) handles = QLIST_HEAD_INITIALIZER(handles);
static QemuMutex instr_lock;


static InstrHandle *handle_new(const char **id)
{
    /* instr_lock is locked */
    InstrHandle *res = g_malloc0(sizeof(InstrHandle));
    if (!*id) {
        *id = g_strdup_printf("lib%d", handle_auto_id);
        handle_auto_id++;
    }
    res->id = g_strdup(*id);
    QLIST_INSERT_HEAD(&handles, res, list);
    return res;
}

static void handle_destroy(InstrHandle *handle)
{
    /* instr_lock is locked */
    QLIST_REMOVE(handle, list);
    g_free(handle->id);
    g_free(handle);
}

static InstrHandle *handle_find(const char *id)
{
    /* instr_lock is locked */
    InstrHandle *handle;
    QLIST_FOREACH(handle, &handles, list) {
        if (strcmp(handle->id, id) == 0) {
            return handle;
        }
    }
    return NULL;
}

InstrLoadError instr_load(const char *path, int argc, const char **argv,
                          const char **id)
{
    InstrLoadError res;
    InstrHandle *handle;
    int (*main_cb)(int, const char **);
    int main_res;

    qemu_rec_mutex_lock(&instr_lock);

    if (*id && handle_find(*id)) {
        res = INSTR_LOAD_ID_EXISTS;
        goto out;
    }

    if (!QLIST_EMPTY(&handles) > 0) {
        /* XXX: This is in fact a hard-coded limit, but there's no reason why a
         *      real multi-library implementation should fail.
         */
        res = INSTR_LOAD_TOO_MANY;
        goto out;
    }

    handle = handle_new(id);
    handle->dlhandle = dlopen(path, RTLD_NOW);
    if (handle->dlhandle == NULL) {
        res = INSTR_LOAD_DLERROR;
        goto err;
    }

    main_cb = dlsym(handle->dlhandle, "main");
    if (main_cb == NULL) {
        res = INSTR_LOAD_DLERROR;
        goto err;
    }
    instr_set_event(fini_fn, NULL);

    instr_set_state(INSTR_STATE_ENABLE);
    main_res = main_cb(argc, argv);
    instr_set_state(INSTR_STATE_DISABLE);

    if (main_res != 0) {
        res = INSTR_LOAD_ERROR;
        goto err;
    }

    cpu_list_lock();
    CPUState *cpu;
    CPU_FOREACH(cpu) {
        instr_guest_cpu_enter(cpu);
    }
    cpu_list_unlock();

    res = INSTR_LOAD_OK;
    goto out;

err:
    handle_destroy(handle);
out:
    qemu_rec_mutex_unlock(&instr_lock);
    return res;
}

InstrUnloadError instr_unload(const char *id)
{
    InstrUnloadError res;

    qemu_rec_mutex_lock(&instr_lock);

    InstrHandle *handle = handle_find(id);
    if (handle == NULL) {
        res = INSTR_UNLOAD_INVALID;
        goto out;
    }

    qi_fini_fn fini_fn = instr_get_event(fini_fn);
    if (fini_fn) {
        void *fini_data = instr_get_event(fini_data);
        fini_fn(fini_data);
    }

    instr_set_event(fini_fn, NULL);
    instr_set_event(guest_cpu_enter, NULL);

    /* this should never fail */
    if (dlclose(handle->dlhandle) < 0) {
        res = INSTR_UNLOAD_DLERROR;
    } else {
        res = INSTR_UNLOAD_OK;
    }
    handle_destroy(handle);

out:
    qemu_rec_mutex_unlock(&instr_lock);
    return res;
}

InstrUnloadError instr_unload_all(void)
{
    InstrUnloadError res = INSTR_UNLOAD_OK;

    qemu_rec_mutex_lock(&instr_lock);
    while (true) {
        InstrHandle *handle = QLIST_FIRST(&handles);
        if (handle == NULL) {
            break;
        } else {
            res = instr_unload(handle->id);
            if (res != INSTR_UNLOAD_OK) {
                break;
            }
        }
    }
    qemu_rec_mutex_unlock(&instr_lock);

    return res;
}

static void __attribute__((constructor)) instr_lock_init(void)
{
    qemu_rec_mutex_init(&instr_lock);
}
