/*
 * Copyright © 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_OBJECT_H
#define REMOTE_OBJECT_H

#include "io/channel.h"
#include "qemu/notify.h"

#define TYPE_REMOTE_OBJECT "x-remote-object"
#define REMOTE_OBJECT(obj) \
    OBJECT_CHECK(RemoteObject, (obj), TYPE_REMOTE_OBJECT)
#define REMOTE_OBJECT_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RemoteObjectClass, (obj), TYPE_REMOTE_OBJECT)
#define REMOTE_OBJECT_CLASS(klass) \
    OBJECT_CLASS_CHECK(RemoteObjectClass, (klass), TYPE_REMOTE_OBJECT)

typedef struct {
    ObjectClass parent_class;

    unsigned int nr_devs;
    unsigned int max_devs;
} RemoteObjectClass;

typedef struct {
    /* private */
    Object parent;

    Notifier machine_done;

    int fd;
    char *devid;
    QIOChannel *ioc;
} RemoteObject;

#endif
