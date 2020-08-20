/*
 * VMState interface
 *
 * Copyright (c) 2009-2019 Red Hat Inc
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/vmstate-if.h"

static const TypeInfo vmstate_if_info = {
    .name = TYPE_VMSTATE_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(VMStateIfClass),
};
TYPE_INFO(vmstate_if_info)


