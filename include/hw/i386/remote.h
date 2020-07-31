/*
 * Remote machine configuration
 *
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_MACHINE_H
#define REMOTE_MACHINE_H

#include "qom/object.h"
#include "hw/boards.h"
#include "hw/pci-host/remote.h"
#include "io/channel.h"
#include "hw/remote/iohub.h"

typedef struct RemoteMachineState {
    MachineState parent_obj;

    RemotePCIHost *host;

    RemoteIOHubState iohub;
} RemoteMachineState;

#define TYPE_REMOTE_MACHINE "remote-machine"
#define REMOTE_MACHINE(obj) \
    OBJECT_CHECK(RemoteMachineState, (obj), TYPE_REMOTE_MACHINE)

gboolean mpqemu_process_msg(QIOChannel *ioc, GIOCondition cond,
                            gpointer opaque);

#endif
