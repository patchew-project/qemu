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

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "qom/object.h"
#include "hw/boards.h"
#include "hw/pci-host/remote.h"
#include "io/channel.h"

typedef struct RemMachineState {
    MachineState parent_obj;

    RemotePCIHost *host;
    QIOChannel *ioc;
} RemMachineState;

#define TYPE_REMOTE_MACHINE "remote-machine"
#define REMOTE_MACHINE(obj) \
    OBJECT_CHECK(RemMachineState, (obj), TYPE_REMOTE_MACHINE)

#endif
