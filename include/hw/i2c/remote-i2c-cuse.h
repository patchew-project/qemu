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
#ifndef HW_REMOTE_I2C_MASTER_BC_CUSE_H
#define HW_REMOTE_I2C_MASTER_BC_CUSE_H

#include "qom/object_interfaces.h"
#include "qapi/qapi-builtin-types.h"
#include "hw/i2c/remote-i2c-backend.h"

/* OS and Transport specifics stay HERE */
#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <fuse3/cuse_lowlevel.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define TYPE_REMOTE_I2C_BACKEND_CUSE "remote-i2c-backend-cuse"
OBJECT_DECLARE_SIMPLE_TYPE(RemoteI2CBackendCuse, REMOTE_I2C_BACKEND_CUSE)

typedef enum i2c_ioctl_state {
    I2C_IOCTL_START,
    I2C_IOCTL_GET,
    I2C_IOCTL_RECV,
    I2C_IOCTL_SEND,
    I2C_IOCTL_FINISHED,
} i2c_ioctl_state;

struct remote_i2c_in_data {
    unsigned int last_cmd;
    fuse_req_t req;
    const struct i2c_smbus_ioctl_data *in_smbus_data;
    const struct i2c_rdwr_ioctl_data *in_rdwr_data;
    void *in_buf;
};

struct RemoteI2CBackendCuse {
    RemoteI2CBackend parent_obj;

    /* Config Properties */
    char *devname;
    strList *fuse_opts;

    /* FUSE Session State */
    AioContext *ctx;
    struct fuse_session *fuse_session;
    struct fuse_buf fuse_buf;
    bool is_open;

    /* CUSE IOCTL State Helpers */
    i2c_ioctl_state ioctl_state;
    uint32_t last_ioctl;
    struct remote_i2c_in_data in_data;
    bool smbus_restart_read;

    /* RDWR OS-Level Support */
    struct i2c_msg *rdwr_msgs;
    uint32_t nmsgs;
    uint32_t msg_idx;
    size_t rdwr_data_offset;
    size_t rdwr_in_buf_size;
    uint8_t rdwr_out_buf[REMOTE_I2C_BACKEND_RDWR_BUF_LEN];
    size_t rdwr_out_len;
};

/* CUSE-Specific Prototypes */
int remote_i2c_fuse_export(RemoteI2CBackendCuse *cuse, Error **errp);

#endif /* HW_REMOTE_I2C_MASTER_BC_CUSE_H */
