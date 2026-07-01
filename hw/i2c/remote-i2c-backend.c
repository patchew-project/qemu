// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Remote I2C Backend (Abstract Base Class)
 *
 * This module defines the abstract backend interface for the Remote I2C Master.
 * It provides the QEMU Object Model (QOM) base class that strictly decouples
 * the internal QEMU I2C hardware state machine (the frontend) from
 * host-specific transport layers.
 *
 * Author:
 * Ilya Chichkov <ilya.chichkov.dev@gmail.com>
 *
 */
#include "qemu/osdep.h"
#include "hw/i2c/remote-i2c-backend.h"

static const TypeInfo remote_i2c_backend_info = {
    .name = TYPE_REMOTE_I2C_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(RemoteI2CBackend),
    .class_size = sizeof(RemoteI2CBackendClass),
    .abstract = true,
};

static void remote_i2c_backend_register_types(void)
{
    type_register_static(&remote_i2c_backend_info);
}

type_init(remote_i2c_backend_register_types)
