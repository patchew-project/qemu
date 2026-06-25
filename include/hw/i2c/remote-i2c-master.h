// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Remote I2C Master
 *
 * This device exposes a QEMU I2C bus to the host system via a FUSE (CUSE)
 * character device. It allows external programs or scripts on the host to
 * interact with I2C slaves simulated inside QEMU as if they were real hardware
 * devices attached to the host.
 *
 * Features:
 * - Implements the Linux I2C ioctl interface (I2C_RDWR, I2C_SMBUS).
 * - Supports standard I2C and SMBus protocols.
 * - Handles asynchronous I2C transactions via QEMU's Bottom Halves (BH).
 * - Supports SMBus "Repeated Start" for atomic Write-then-Read operations.
 *
 * Usage:
 * Add the device to QEMU:
 * "-device remote-i2c-master,i2cbus=i2c-bus.0,devname=i2c-33"
 * This creates a character device at /dev/i2c-33 on the host.
 * Use standard tools (i2c-tools) or C programs to access it:
 * i2cdetect -y -l
 * i2cget -y <bus_id> <addr> <reg>
 *
 * Author:
 * Ilya Chichkov <ilya.chichkov.dev@gmail.com>
 *
 */
#ifndef HW_REMOTE_I2C_MASTER_H
#define HW_REMOTE_I2C_MASTER_H

#include "hw/sysbus.h"
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include "hw/i2c/remote-i2c-backend.h"

#define TYPE_REMOTE_I2C_MASTER "remote-i2c-master"

#define REMOTE_I2C_MASTER(obj) \
    OBJECT_CHECK(RemoteI2CMasterState, (obj), TYPE_REMOTE_I2C_MASTER)

typedef enum {
    REMOTE_I2C_CMD_START_TX,
    REMOTE_I2C_CMD_NEXT_MSG,
    REMOTE_I2C_CMD_ABORT,
    REMOTE_I2C_CMD_RESET
} RemoteI2CCommand;

typedef struct RemoteI2CMasterState {
    DeviceState parent_obj;

    I2CBus *bus;
    char *name;

    /* QOM Link to the abstract Backend */
    RemoteI2CBackend *backend;

    QEMUTimer *timer;
    QEMUTimer *timer_start_transmit;
    QEMUTimer *timer_step;
    QEMUTimer *slow_master_timer;
    QEMUBH *bh;
    MemReentrancyGuard bh_guard;

    uint32_t timeout_ms;
    uint32_t default_stretch_delay_ms;
    bool slow_delay_enable;
    uint32_t slow_delay_value_ms;
    bool raise_arbitrage_lost;
    bool delay_completed;

} RemoteI2CMasterState;

void remote_i2c_fsm_dispatch(RemoteI2CMasterState *s, RemoteI2CCommand cmd);
void remote_i2c_fsm_timer_start_transmit_cb(void *opaque);
void remote_i2c_fsm_bh(void *opaque);

#endif /* HW_REMOTE_I2C_MASTER_H */
