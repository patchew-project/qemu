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
#ifndef HW_REMOTE_I2C_BACKEND_H
#define HW_REMOTE_I2C_BACKEND_H

#include "qemu/osdep.h"
#include "qom/object.h"

typedef enum i2c_bus_state {
    I2C_BUS_IDLE,
    I2C_BUS_ADDR,
    I2C_BUS_SEND,
    I2C_BUS_RECV,
    I2C_BUS_END,
    I2C_BUS_FINISHED,
    I2C_BUS_WAIT_STRETCH,
    I2C_BUS_WAIT_RELEASE,
} i2c_bus_state;

typedef struct RemoteI2CMasterState RemoteI2CMasterState;

#define TYPE_REMOTE_I2C_BACKEND "remote-i2c-backend"
OBJECT_DECLARE_TYPE(RemoteI2CBackend, RemoteI2CBackendClass, REMOTE_I2C_BACKEND)

#define REMOTE_I2C_BACKEND_BUF_LEN      256
#define REMOTE_I2C_BACKEND_RDWR_BUF_LEN 260

struct RemoteI2CBackend {
    Object parent_obj;

    RemoteI2CMasterState *frontend;

    /* Transaction State */
    uint8_t  transaction_buf[REMOTE_I2C_BACKEND_BUF_LEN];
    uint16_t transaction_index;
    uint16_t transaction_length;

    bool is_recv;
    bool is_master_pending;
    bool is_transaction_failed;
    bool is_slave_async;
    bool waiting_for_async;
    bool addr_acked;
    bool data_acked;
    bool timed_out;
    uint32_t timeout_ms;

    i2c_bus_state bus_state;
    long address;
};

struct RemoteI2CBackendClass {
    ObjectClass parent_class;

    void (*on_tx_complete)(RemoteI2CBackend *backend);
    void (*on_tx_error)(RemoteI2CBackend *backend, int errno_code);
};

#endif /* HW_REMOTE_I2C_BACKEND_H */
