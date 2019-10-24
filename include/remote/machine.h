/*
 * Remote machine configuration
 *
 * Copyright 2019, Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef REMOTE_MACHINE_H
#define REMOTE_MACHINE_H

#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/boards.h"
#include "remote/pcihost.h"
#include "qemu/notify.h"

typedef struct RemMachineState {
    MachineState parent_obj;

    RemPCIHost *host;
} RemMachineState;

#define TYPE_REMOTE_MACHINE "remote-machine"
#define REMOTE_MACHINE(obj) \
    OBJECT_CHECK(RemMachineState, (obj), TYPE_REMOTE_MACHINE)

void qemu_run_machine_init_done_notifiers(void);

#endif
