// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Remote I2C CUSE Backend
 *
 * This module provides the concrete CUSE (Character device in Userspace)
 * implementation of the abstract Remote I2C Backend interface. It acts as the
 * physical bridge between the Linux host's I2C subsystem and QEMU's internal
 * I2C hardware emulation.
 *
 * Architecture & Responsibilities:
 * - Inherits from the abstract `RemoteI2CBackend` QOM base class.
 * - Initializes and manages the FUSE/CUSE session, integrating its file
 *   descriptors directly into QEMU's main AioContext event loop.
 * - Translates Linux user-space IOCTLs (I2C_RDWR, I2C_SMBUS, I2C_SLAVE) into
 *   generic byte streams for the Master Frontend to process.
 * - Implements the `on_tx_complete` and `on_tx_error` virtual callbacks to
 *   format QEMU's response data back into Linux-compatible I2C/SMBus data
 *   structures and reply to the FUSE driver.
 *
 * Usage:
 * Instantiated via the QEMU CLI as a backend object:
 *   -object remote-i2c-backend-cuse,
 *           id=<id>,devname=<node_name>[,fuse-opts=<opts>]
 *
 * Author:
 * Ilya Chichkov <ilya.chichkov.dev@gmail.com>
 *
 */
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties-system.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "block/aio.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "trace.h"

#include "hw/i2c/remote-i2c-cuse.h"
#include "hw/i2c/remote-i2c-master.h" /* For FSM dispatch and commands */

#define I2C_BUS_BUSY_CHECK_TIMER_COOLDOWN_NS 50000

#define I2C_BUFFER_INDEX_SIZE 2
#define I2C_BUFFER_INDEX_COMMAND 3
#define I2C_BUFFER_INDEX_DATA_0 4
#define I2C_BUFFER_INDEX_DATA_1 5


static bool cuse_get_debug(Object *obj, Error **errp)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(obj);
    return cuse->debug;
}

static void cuse_set_debug(Object *obj, bool value, Error **errp)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(obj);
    cuse->debug = value;
}

static char *cuse_get_devname(Object *obj, Error **errp)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(obj);
    return g_strdup(cuse->devname);
}

static void cuse_set_devname(Object *obj, const char *value, Error **errp)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(obj);
    g_free(cuse->devname);
    cuse->devname = g_strdup(value);
}

static char *cuse_get_fuse_opts(Object *obj, Error **errp)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(obj);
    return g_strdup(cuse->fuse_opts);
}

static void cuse_set_fuse_opts(Object *obj, const char *value, Error **errp)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(obj);
    g_free(cuse->fuse_opts);
    cuse->fuse_opts = g_strdup(value);
}


/*
 * remote_i2c_serialize_smbus_write:
 * @cuse: The concrete CUSE backend instance.
 * @in_val: Pointer to the incoming Linux SMBus IOCTL data structure.
 *
 * Translates a Linux user-space SMBus write or read command request into a flat
 * payload array compatible with QEMU's generic I2C transaction buffer
 */
static
void remote_i2c_serialize_smbus_write(RemoteI2CBackendCuse *cuse,
                                      const struct i2c_smbus_ioctl_data *in_val)
{
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);
    union i2c_smbus_data data;
    uint8_t buf[REMOTE_I2C_BACKEND_BUF_LEN] = { 0 };
    uint8_t len = 0;

    memset(backend->transaction_buf, 0, REMOTE_I2C_BACKEND_BUF_LEN);

    buf[0] = in_val->read_write;
    buf[1] = (uint8_t)backend->address;

    memcpy(&data,
           cuse->in_data.in_buf + sizeof(struct i2c_smbus_ioctl_data),
           sizeof(union i2c_smbus_data));

    if (in_val->read_write == I2C_SMBUS_READ) {
        if (in_val->size == I2C_SMBUS_BYTE) {
            backend->transaction_length = 0;
            return;
        }

        backend->transaction_length = 1;
        backend->transaction_buf[0] = in_val->command;
        return;
    }

    switch (in_val->size) {
    case I2C_SMBUS_QUICK:
        buf[I2C_BUFFER_INDEX_SIZE] = 0;
        break;
    case I2C_SMBUS_BYTE:
        buf[I2C_BUFFER_INDEX_SIZE] = 1;
        buf[I2C_BUFFER_INDEX_COMMAND] = in_val->command;
        break;
    case I2C_SMBUS_BYTE_DATA:
        buf[I2C_BUFFER_INDEX_SIZE] = 2;
        buf[I2C_BUFFER_INDEX_COMMAND] = in_val->command;
        buf[I2C_BUFFER_INDEX_DATA_0] = data.byte;
        break;
    case I2C_SMBUS_WORD_DATA:
    case I2C_SMBUS_PROC_CALL:
        buf[I2C_BUFFER_INDEX_SIZE] = 3;
        buf[I2C_BUFFER_INDEX_COMMAND] = in_val->command;
        buf[I2C_BUFFER_INDEX_DATA_0] = (uint8_t)(data.word & 0xFF);
        buf[I2C_BUFFER_INDEX_DATA_1] = (uint8_t)(data.word >> 8 & 0xFF);
        break;
    case I2C_SMBUS_BLOCK_DATA:
    case I2C_SMBUS_I2C_BLOCK_BROKEN:
    case I2C_SMBUS_I2C_BLOCK_DATA:
    case I2C_SMBUS_BLOCK_PROC_CALL:
        {
            len = MIN(data.block[0], I2C_SMBUS_BLOCK_MAX);
            buf[I2C_BUFFER_INDEX_SIZE] = len + 2; /* Command + Count + Data */
            buf[I2C_BUFFER_INDEX_COMMAND] = in_val->command;
            buf[I2C_BUFFER_INDEX_DATA_0] = len;
            memcpy(&buf[I2C_BUFFER_INDEX_DATA_1], &data.block[1], len);
        }
        break;
    default:
        buf[I2C_BUFFER_INDEX_SIZE] = 0;
        break;
    }

    backend->transaction_length = buf[I2C_BUFFER_INDEX_SIZE];
    memcpy(backend->transaction_buf, &buf[I2C_BUFFER_INDEX_COMMAND],
           backend->transaction_length);
}

/*
 * remote_i2c_calculate_expected_recv_len:
 * @cuse: The concrete CUSE backend instance.
 *
 * Inspects active IOCTL contexts (either SMBus reads or standard I2C_RDWR
 * message arrays) to calculate the exact number of bytes expected to be read
 * from the emulated I2C target.
 */
static void remote_i2c_calculate_expected_recv_len(RemoteI2CBackendCuse *cuse)
{
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);
    union i2c_smbus_data *smbus_data;
    uint16_t size = 0;

    if (cuse->in_data.last_cmd == I2C_SMBUS) {
        size = cuse->in_data.in_smbus_data->size;
        smbus_data = (union i2c_smbus_data *)(
            cuse->in_data.in_buf + sizeof(struct i2c_smbus_ioctl_data)
        );

        switch (size) {
        case I2C_SMBUS_QUICK:
            backend->transaction_length = 0;
            break;
        case I2C_SMBUS_BYTE:
        case I2C_SMBUS_BYTE_DATA:
            backend->transaction_length = 1;
            break;
        case I2C_SMBUS_WORD_DATA:
        case I2C_SMBUS_PROC_CALL:
            backend->transaction_length = 2;
            break;
        case I2C_SMBUS_BLOCK_DATA:
        case I2C_SMBUS_I2C_BLOCK_BROKEN:
        case I2C_SMBUS_I2C_BLOCK_DATA:
        case I2C_SMBUS_BLOCK_PROC_CALL:
            backend->transaction_length = smbus_data->block[0];
            if (backend->transaction_length == 0) {
                backend->transaction_length = I2C_SMBUS_BLOCK_MAX;
            }
            break;
        default:
            backend->transaction_length = 0;
            break;
        }
    } else if (cuse->in_data.last_cmd == I2C_RDWR) {
        if (cuse->rdwr_msgs) {
            backend->transaction_length = cuse->rdwr_msgs[cuse->msg_idx].len;
        }
    }
}

/*
 * remote_i2c_deserialize_smbus_read:
 * @cuse: The concrete CUSE backend instance.
 *
 * Packages raw bytes retrieved from QEMU's emulated I2C target back into the
 * appropriate native Linux SMBus data union format.
 */
static void remote_i2c_deserialize_smbus_read(RemoteI2CBackendCuse *cuse)
{
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);
    union i2c_smbus_data *smbus_data = (union i2c_smbus_data *)(
        cuse->in_data.in_buf + sizeof(struct i2c_smbus_ioctl_data)
    );
    uint16_t size = cuse->in_data.in_smbus_data->size;
    uint8_t len = 0;

    switch (size) {
    case I2C_SMBUS_BYTE:
    case I2C_SMBUS_BYTE_DATA:
        smbus_data->byte = backend->transaction_buf[0];
        break;
    case I2C_SMBUS_WORD_DATA:
    case I2C_SMBUS_PROC_CALL:
        smbus_data->word = ((uint16_t)backend->transaction_buf[0]) & 0xFF;
        smbus_data->word |=
            (((uint16_t)backend->transaction_buf[1]) << 8) & 0xFF00;
        break;
    case I2C_SMBUS_BLOCK_DATA:
    case I2C_SMBUS_BLOCK_PROC_CALL:
        len = MIN(backend->transaction_buf[0], I2C_SMBUS_BLOCK_MAX);
        smbus_data->block[0] = len;
        memcpy(&smbus_data->block[1], &backend->transaction_buf[1], len);
        break;
    case I2C_SMBUS_I2C_BLOCK_DATA:
    case I2C_SMBUS_I2C_BLOCK_BROKEN:
        len = MIN(backend->transaction_length, I2C_SMBUS_BLOCK_MAX);
        smbus_data->block[0] = len;
        memcpy(&smbus_data->block[1], backend->transaction_buf, len);
        break;
    default:
        break;
    }

    fuse_reply_ioctl(cuse->in_data.req, 0, smbus_data, sizeof(*smbus_data));
}

/*
 * remote_i2c_update_slave_address:
 * @cuse: The concrete CUSE backend instance.
 * @req: The active FUSE request context handle.
 * @address: The 7-bit target I2C slave address requested by the host OS.
 *
 * Validates and updates the transaction target device address in the shared
 * backend state.
 */
static
void remote_i2c_update_slave_address(RemoteI2CBackendCuse *cuse,
                                     fuse_req_t req, long address)
{
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);

    if (address < 0 || address > 127) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    backend->address = address;
    trace_remote_i2c_master_i2cdev_address(backend->address);
}

/*
 * remote_i2c_advance_rdwr_sequence:
 * @cuse: The concrete CUSE backend instance.
 *
 * Iterates through the batch vector array of standard native Linux `i2c_msg`
 * blocks received during a multi-message I2C_RDWR ioctl operation. Populates
 * the next message's direction, length, and payload buffer into the generic
 * transaction state machine, advancing data offsets and preparing the master
 * frontend bus state for an updated address phase.
 */
static void remote_i2c_advance_rdwr_sequence(RemoteI2CBackendCuse *cuse)
{
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);
    const struct i2c_msg *current_msg = NULL;

    if (!cuse->rdwr_msgs || cuse->msg_idx >= cuse->nmsgs) {
        backend->bus_state = I2C_BUS_END;
        return;
    }

    current_msg = &cuse->rdwr_msgs[cuse->msg_idx];
    remote_i2c_update_slave_address(cuse, cuse->in_data.req,
                                    (long)current_msg->addr);

    backend->transaction_length = current_msg->len;
    backend->transaction_index = 0;
    backend->addr_acked = false;
    backend->data_acked = false;
    backend->timed_out = false;

    /* Evaluate sequence direction and staging boundaries */
    if (current_msg->flags & I2C_M_RD) {
        backend->is_recv = true;
    } else {
        backend->is_recv = false;

        if ((cuse->rdwr_data_offset + current_msg->len) <=
            cuse->rdwr_in_buf_size) {
            memcpy(backend->transaction_buf,
                   cuse->in_data.in_buf + cuse->rdwr_data_offset,
                   current_msg->len);
            cuse->rdwr_data_offset += current_msg->len;
        } else {
            backend->is_transaction_failed = true;
            backend->bus_state = I2C_BUS_FINISHED;

            qemu_log_mask(LOG_GUEST_ERROR,
                          "Remote I2C Backend: Buffer overflow during RDWR "
                          "deserialization. Message requests %u bytes, but "
                          "only %zu bytes remain.\n",
                          current_msg->len,
                          (cuse->rdwr_in_buf_size - cuse->rdwr_data_offset));
            return;
        }
    }

    backend->bus_state = I2C_BUS_ADDR;
}

/*
 * remote_i2c_cuse_on_tx_complete:
 * @backend: Pointer to the abstract RemoteI2CBackend base structure.
 *
 * Implements the virtual execution callback triggered by the frontend master
 * FSM when a given hardware I2C sub-transaction completes successfully.
 * Manages protocol sequence vector chaining (such as multi-message RDWR
 * arrays or atomic SMBus Write-then-Read phases). Once all phases complete,
 * it packages retrieved byte payloads and terminates the active host OS IOCTL
 * over FUSE.
 */
static void remote_i2c_cuse_on_tx_complete(RemoteI2CBackend *backend)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(backend);
    size_t available_space = 0;
    size_t copy_len = 0;

    /*
     * If we were receiving data during an RDWR,
     * copy it into the output buffer
     */
    if (cuse->in_data.last_cmd == I2C_RDWR && backend->is_recv) {
        available_space = REMOTE_I2C_BACKEND_RDWR_BUF_LEN - cuse->rdwr_out_len;
        copy_len = (backend->transaction_length < available_space) ?
                          backend->transaction_length : available_space;

        if (copy_len > 0) {
            memcpy(cuse->rdwr_out_buf + cuse->rdwr_out_len,
                   backend->transaction_buf,
                   copy_len);
            cuse->rdwr_out_len += copy_len;
        }
    }

    /* Process Multi-Message Protocol Chaining Vectors */
    if (cuse->in_data.last_cmd == I2C_SMBUS &&
        cuse->smbus_restart_read &&
        !backend->is_recv) {
        backend->is_recv = true;
        backend->transaction_index = 0;
        remote_i2c_calculate_expected_recv_len(cuse);
        remote_i2c_fsm_dispatch(backend->frontend, REMOTE_I2C_CMD_NEXT_MSG);
        return;
    } else if (cuse->in_data.last_cmd == I2C_RDWR) {
        cuse->msg_idx++;

        if (cuse->msg_idx < cuse->nmsgs) {
            remote_i2c_advance_rdwr_sequence(cuse);
            remote_i2c_fsm_dispatch(backend->frontend, REMOTE_I2C_CMD_NEXT_MSG);
            return;
        }
    }

    if (cuse->in_data.last_cmd == I2C_RDWR) {
        fuse_reply_ioctl(cuse->in_data.req, cuse->nmsgs,
                         cuse->rdwr_out_buf, cuse->rdwr_out_len);
    } else if (cuse->in_data.last_cmd == I2C_SMBUS && backend->is_recv) {
        if (backend->transaction_length > 0) {
            remote_i2c_deserialize_smbus_read(cuse);
        } else {
            fuse_reply_ioctl(cuse->in_data.req, 0, NULL, 0);
        }
    } else {
        fuse_reply_ioctl(cuse->in_data.req, 0, NULL, 0);
    }

    if (cuse->in_data.in_buf) {
        g_free((gpointer)cuse->in_data.in_buf);
        cuse->in_data.in_buf = NULL;
        cuse->in_data.in_smbus_data = NULL;
    }

    cuse->ioctl_state = I2C_IOCTL_START;
    cuse->last_ioctl = 0;
    cuse->smbus_restart_read = false;
}

/*
 * remote_i2c_cuse_reset_session:
 * @cuse: The concrete CUSE backend instance.
 *
 * Internal helper to teardown and clear temporary transaction contexts.
 */
static void remote_i2c_cuse_reset_session(RemoteI2CBackendCuse *cuse)
{
    if (cuse->in_data.in_buf) {
        g_free((gpointer)cuse->in_data.in_buf);
        cuse->in_data.in_buf = NULL;
        cuse->in_data.in_smbus_data = NULL;
    }

    cuse->ioctl_state = I2C_IOCTL_START;
    cuse->last_ioctl = 0;
    cuse->smbus_restart_read = false;
}

/*
 * remote_i2c_cuse_on_tx_error:
 * @backend: Pointer to the abstract RemoteI2CBackend base structure.
 * @errno_code: The POSIX error number reflecting the nature of the failure.
 *
 * Implements the virtual execution callback triggered by the frontend master
 * FSM when a hardware I2C transaction fails or aborts.
 */
static
void remote_i2c_cuse_on_tx_error(RemoteI2CBackend *backend, int errno_code)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(backend);

    fuse_reply_err(cuse->in_data.req, errno_code);
    remote_i2c_cuse_reset_session(cuse);
}

/*
 * remote_i2c_cuse_init:
 * @userdata: Arbitrary reference pointer registered at session startup.
 * @conn: Struct mapping runtime driver features and limits for the connection.
 *
 * Virtual callback invoked by the FUSE infrastructure once the userspace
 * character device mapping handshake completes successfully.
 */
static void remote_i2c_cuse_init(void *userdata, struct fuse_conn_info *conn)
{
    (void)userdata;
    trace_remote_i2c_master_i2cdev_init();
}

/*
 * remote_i2c_cuse_open:
 * @req: The active FUSE request context handle.
 * @fi: Driver metadata tracker for the targeted host file descriptor.
 *
 * Configures the communication channel when a host application requests
 * access to the virtual character node (e.g., /dev/i2c-33). Clears baseline
 * tracking flags, normalizes backend engine states, and invokes a safe reset
 * of all active tracking buffers.
 */
static void remote_i2c_cuse_open(fuse_req_t req, struct fuse_file_info *fi)
{
    RemoteI2CBackendCuse *cuse = fuse_req_userdata(req);
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);

    cuse->is_open = true;
    cuse->ioctl_state = I2C_IOCTL_START;
    cuse->last_ioctl = 0;

    backend->bus_state = I2C_BUS_IDLE;
    backend->is_recv = false;
    backend->waiting_for_async = false;
    backend->timed_out = false;

    if (cuse->in_data.in_buf) {
        g_free((gpointer)cuse->in_data.in_buf);
        cuse->in_data.in_buf = NULL;
    }

    cuse->rdwr_msgs = NULL;

    fuse_reply_open(req, fi);
    trace_remote_i2c_master_i2cdev_open();
}

/*
 * remote_i2c_cuse_release:
 * @req: The active FUSE request context handle.
 * @fi: Driver metadata tracker for the targeted host file descriptor.
 *
 * Handles formal close/teardown requests from user-space applications.
 * Tears down mapping tables, releases structural heap components to avoid
 * memory leaks, and resets the channel boundary flags to unlinked defaults.
 */
static void remote_i2c_cuse_release(fuse_req_t req, struct fuse_file_info *fi)
{
    RemoteI2CBackendCuse *cuse = fuse_req_userdata(req);
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);

    cuse->is_open = false;
    cuse->ioctl_state = I2C_IOCTL_START;
    cuse->last_ioctl = 0;

    backend->bus_state = I2C_BUS_IDLE;

    g_free(cuse->in_data.in_buf);
    cuse->in_data.in_buf = NULL;

    cuse->rdwr_msgs = NULL;

    fuse_reply_err(req, 0);
    trace_remote_i2c_master_i2cdev_release();
}

/*
 * remote_i2c_cuse_read:
 * @req: The active FUSE request context handle.
 * @size: Byte count window requested by the caller.
 * @off: Seek offset identifier within the streaming channel context.
 * @fi: Driver metadata tracker for the targeted host file descriptor.
 *
 * Implements standard char-node read interfaces. Because physical
 * I2C operations are handled strictly via target-directed IOCTL vectors,
 * this entry point is standard-compliant stub code returning 0 bytes (EOF).
 */
static void remote_i2c_cuse_read(fuse_req_t req, size_t size, off_t off,
                                 struct fuse_file_info *fi)
{
    (void)size;
    (void)off;
    (void)fi;
    fuse_reply_buf(req, NULL, 0);
}

/*
 * remote_i2c_cuse_functional:
 * @cuse: The concrete CUSE backend instance.
 * @req: The active FUSE request context handle.
 * @arg: Host memory location reference where data payload
 *       outputs will be staged.
 * @in_buf: Unused trailing verification packet buffer context.
 *
 * Implements the Linux I2C_FUNCS capability negotiation IOCTL
 * interface. Emulates a two-phase FUSE configuration state loop: first
 * prompts the kernel driver to fetch memory staging regions, and
 * subsequently delivers the bitmask array defining what I2C/SMBus protocols
 * this device translates.
 */
static void remote_i2c_cuse_functional(RemoteI2CBackendCuse *cuse,
                                       fuse_req_t req,
                                       void *arg,
                                       const void *in_buf)
{
    unsigned long backend_functionality_mask = (
        I2C_FUNC_I2C | I2C_FUNC_SMBUS_QUICK |
        I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
        I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_WORD_DATA |
        I2C_FUNC_SMBUS_I2C_BLOCK
    );

    struct iovec target_memory_vector = {
        .iov_base = arg,
        .iov_len = sizeof(unsigned long)
    };

    switch (cuse->ioctl_state) {
    case I2C_IOCTL_START:
        cuse->ioctl_state = I2C_IOCTL_GET;
        fuse_reply_ioctl_retry(req, NULL, 0, &target_memory_vector, 1);
        break;
    case I2C_IOCTL_GET:
        fuse_reply_ioctl(req, 0, &backend_functionality_mask,
                         sizeof(backend_functionality_mask));
        cuse->ioctl_state = I2C_IOCTL_FINISHED;
        trace_remote_i2c_master_i2cdev_functional();
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Remote I2C Backend: Invalid IOCTL state "
                      "encountered in functionality query handler: %d\n",
                      cuse->ioctl_state);
        break;
    }
}

/*
 * remote_i2c_cuse_address:
 * @cuse: The concrete CUSE backend instance.
 * @req: The active FUSE request context handle.
 * @arg: Untyped reference mapped by the host kernel conveying the target
 *       address.
 * @in_buf: Unused auxiliary input payload vector.
 *
 * Implements the standard Linux native I2C_SLAVE ioctl entry point.
 */
static void i2cdev_address(RemoteI2CBackendCuse *cuse,
                           fuse_req_t req,
                           void *arg,
                           const void *in_buf)
{
    (void)in_buf;
    remote_i2c_update_slave_address(cuse, req, (long)arg);
    fuse_reply_ioctl(req, 0, NULL, 0);
    cuse->ioctl_state = I2C_IOCTL_FINISHED;
}

/*
 * remote_i2c_cuse_cmd_rdwr:
 * @cuse: The concrete CUSE backend instance.
 * @req: The active FUSE request context handle.
 * @in_arg: Raw input memory reference targeting the calling process space.
 * @in_buf: Staged kernel buffer payload delivered via the asynchronous
 *          FUSE engine.
 * @in_bufsz: Total layout size in bytes of the incoming data block.
 * @out_bufsz: Output tracking limit size reserved by the calling process
 *             framework.
 *
 * Implements the complex multi-phase Linux standard I2C_RDWR ioctl layer.
 * Orchestrates an asynchronous four-phase FUSE data collection handshake loop
 * (START -> GET -> RECV -> SEND) to fetch scatter-gather message vectors and
 * payload blocks directly from host memory space before dispatching execution
 * to the master frontend state machine.
 */
static void i2cdev_cmd_rdwr(RemoteI2CBackendCuse *cuse,
                            fuse_req_t req,
                            void *in_arg,
                            const void *in_buf,
                            size_t in_bufsz,
                            size_t out_bufsz)
{
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);
    struct iovec in_iov[I2C_RDWR_IOCTL_MAX_MSGS + 2];
    struct iovec out_iov[I2C_RDWR_IOCTL_MAX_MSGS];
    const struct i2c_rdwr_ioctl_data *in_val = NULL;
    void *buf_copy = NULL;
    struct i2c_msg *msgs;
    uint32_t out_cnt = 0;
    uint32_t in_cnt = 0;
    size_t header_len;
    uint32_t i = 0;

    if (cuse->ioctl_state == I2C_IOCTL_START) {
        in_iov[0].iov_base = in_arg;
        in_iov[0].iov_len = sizeof(struct i2c_rdwr_ioctl_data);
        fuse_reply_ioctl_retry(req, in_iov, 1, NULL, 0);
        cuse->ioctl_state = I2C_IOCTL_GET;
        return;
    }

    if (in_bufsz < sizeof(struct i2c_rdwr_ioctl_data)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    /*
     * Create an isolated local copy of host memory packets for
     * transaction context tracking
     */
    buf_copy = g_memdup2(in_buf, in_bufsz);
    in_val = buf_copy;

    if (cuse->in_data.in_buf) {
        g_free((gpointer)cuse->in_data.in_buf);
    }

    cuse->in_data.last_cmd = I2C_RDWR;
    cuse->in_data.req = req;
    cuse->in_data.in_rdwr_data = in_val;
    cuse->in_data.in_buf = buf_copy;

    switch (cuse->ioctl_state) {
    case I2C_IOCTL_GET:
        if (in_val->nmsgs > I2C_RDWR_IOCTL_MAX_MSGS) {
            fuse_reply_err(req, EINVAL);
            return;
        }
        in_iov[0].iov_base = in_arg;
        in_iov[0].iov_len = sizeof(struct i2c_rdwr_ioctl_data);
        in_iov[1].iov_base = in_val->msgs;
        in_iov[1].iov_len = in_val->nmsgs * sizeof(struct i2c_msg);

        fuse_reply_ioctl_retry(req, in_iov, 2, NULL, 0);
        cuse->ioctl_state = I2C_IOCTL_RECV;
        break;

    case I2C_IOCTL_RECV:
        msgs = (
            (struct i2c_msg *)(
                (uint8_t *)in_buf + sizeof(struct i2c_rdwr_ioctl_data)
            )
        );

        in_iov[in_cnt].iov_base = in_arg;
        in_iov[in_cnt].iov_len = sizeof(struct i2c_rdwr_ioctl_data);
        in_cnt++;

        in_iov[in_cnt].iov_base = in_val->msgs;
        in_iov[in_cnt].iov_len = in_val->nmsgs * sizeof(struct i2c_msg);
        in_cnt++;

        for (i = 0; i < in_val->nmsgs; i++) {
            if (msgs[i].flags & I2C_M_RD) {
                out_iov[out_cnt].iov_base = msgs[i].buf;
                out_iov[out_cnt].iov_len = msgs[i].len;
                out_cnt++;
            } else {
                in_iov[in_cnt].iov_base = msgs[i].buf;
                in_iov[in_cnt].iov_len = msgs[i].len;
                in_cnt++;
            }
        }

        fuse_reply_ioctl_retry(req, in_iov, in_cnt, out_iov, out_cnt);
        cuse->ioctl_state = I2C_IOCTL_SEND;
        break;

    case I2C_IOCTL_SEND:
        header_len = sizeof(struct i2c_rdwr_ioctl_data);
        cuse->nmsgs = in_val->nmsgs;

        cuse->rdwr_msgs = (struct i2c_msg *)((uint8_t *)buf_copy + header_len);
        cuse->rdwr_data_offset = (
            header_len + (cuse->nmsgs * sizeof(struct i2c_msg))
        );
        cuse->rdwr_in_buf_size = in_bufsz;
        cuse->rdwr_out_len = 0;
        cuse->msg_idx = 0;

        remote_i2c_advance_rdwr_sequence(cuse);
        remote_i2c_fsm_dispatch(backend->frontend, REMOTE_I2C_CMD_START_TX);
        break;

    case I2C_IOCTL_FINISHED:
        cuse->ioctl_state = I2C_IOCTL_START;
        cuse->last_ioctl = 0;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Remote I2C Backend: Invalid IOCTL state "
                      "encountered in RDWR handler: %d\n",
                      cuse->ioctl_state);
        break;
    }

    trace_remote_i2c_master_i2cdev_smbus((uint8_t)cuse->ioctl_state);
}

/*
 * remote_i2c_cuse_cmd_smbus:
 * @cuse: The concrete CUSE backend instance.
 * @req: The active FUSE request context handle.
 * @in_arg: Raw input memory reference targeting the calling process
 *          space.
 * @in_buf: Staged kernel buffer payload delivered via the asynchronous
 *          FUSE engine.
 * @in_bufsz: Total layout size in bytes of the incoming data block.
 * @out_bufsz: Output tracking limit size reserved by the calling process
 *             framework.
 *
 * Implements the Linux standard I2C_SMBUS ioctl layer. Orchestrates an
 * asynchronous multi-phase FUSE ioctl state pipeline
 * (START -> GET -> RECV/SEND) to isolate SMBus transaction structures,
 * evaluate specialized transfer cycles (like Write-then-Read Repeated Starts),
 * and format parameters for the shared abstract frontend state engine.
 */
static void remote_i2c_cuse_cmd_smbus(RemoteI2CBackendCuse *cuse,
                                      fuse_req_t req,
                                      void *in_arg,
                                      const void *in_buf,
                                      size_t in_bufsz,
                                      size_t out_bufsz)
{
    RemoteI2CBackend *backend = REMOTE_I2C_BACKEND(cuse);
    const struct i2c_smbus_ioctl_data *in_val = NULL;
    struct iovec in_iov[2];
    size_t full_size = 0;
    void *buf_copy = NULL;

    if (cuse->ioctl_state == I2C_IOCTL_START) {
        in_iov[0].iov_base = in_arg;
        in_iov[0].iov_len = sizeof(struct i2c_smbus_ioctl_data);
        fuse_reply_ioctl_retry(req, in_iov, 1, NULL, 0);
        cuse->ioctl_state = I2C_IOCTL_GET;
        return;
    }

    if (in_bufsz < sizeof(struct i2c_smbus_ioctl_data)) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    full_size = (
        sizeof(struct i2c_smbus_ioctl_data) + sizeof(union i2c_smbus_data)
    );
    buf_copy = g_malloc0(full_size);
    memcpy(buf_copy, in_buf, in_bufsz);

    in_val = buf_copy;

    in_iov[0].iov_base = in_arg;
    in_iov[0].iov_len = sizeof(struct i2c_smbus_ioctl_data);

    cuse->in_data.last_cmd = I2C_SMBUS;
    cuse->in_data.req = req;
    cuse->in_data.in_smbus_data = in_val;
    cuse->in_data.in_buf = buf_copy;

    /*
     * Detect if this is an SMBus Block Read/Process Call that
     * requires a write-then-read chain sequence.
     */
    if (in_val->read_write == I2C_SMBUS_READ &&
        in_val->size != I2C_SMBUS_QUICK && in_val->size != I2C_SMBUS_BYTE) {
        cuse->smbus_restart_read = true;
    } else {
        cuse->smbus_restart_read = false;
    }

    /* Guard check for Quick commands that do not transmit data pointers */
    if (cuse->ioctl_state == I2C_IOCTL_GET && !in_val->read_write) {
        if (!in_val->data) {
            cuse->ioctl_state = I2C_IOCTL_SEND;
        }
    }

    /*
     * Execute subsequent streaming segments of the FUSE memory
     * fetching engine.
     */
    switch (cuse->ioctl_state) {
    case I2C_IOCTL_START:
        break;
    case I2C_IOCTL_GET:
        if (in_val->read_write) {
            struct iovec out_iov = {
                .iov_base = in_val->data,
                .iov_len = sizeof(union i2c_smbus_data)
            };
            fuse_reply_ioctl_retry(req, in_iov, 1, &out_iov, 1);
            cuse->ioctl_state = I2C_IOCTL_RECV;
        } else {
            if (in_val->data) {
                in_iov[1].iov_base = in_val->data;
                in_iov[1].iov_len = sizeof(union i2c_smbus_data);
                fuse_reply_ioctl_retry(req, in_iov, 2, NULL, 0);
            }
            cuse->ioctl_state = I2C_IOCTL_SEND;
        }
        break;
    case I2C_IOCTL_RECV:
    case I2C_IOCTL_SEND:
        backend->is_recv = (cuse->ioctl_state == I2C_IOCTL_RECV);

        /* If a restart read is required, the FIRST phase is always a Write */
        if (cuse->smbus_restart_read) {
            backend->is_recv = false;
        }

        remote_i2c_serialize_smbus_write(cuse, cuse->in_data.in_smbus_data);

        if (backend->is_recv) {
            remote_i2c_calculate_expected_recv_len(cuse);
        }

        remote_i2c_fsm_dispatch(backend->frontend, REMOTE_I2C_CMD_START_TX);
        break;
    case I2C_IOCTL_FINISHED:
        cuse->ioctl_state = I2C_IOCTL_START;
        cuse->last_ioctl = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Remote I2C Backend: Invalid IOCTL state "
                      "encountered in SMBus handler: %d\n",
                      cuse->ioctl_state);
        break;
    }

    trace_remote_i2c_master_i2cdev_smbus((uint8_t)cuse->ioctl_state);
}

/*
 * remote_i2c_cuse_ioctl:
 * @req: The active FUSE request context handle.
 * @cmd: The specific Linux IOCTL command numeric ID arriving from the host
 *       kernel.
 * @arg: Untyped data pointer mapping process-space context memory arguments.
 * @fi: Driver metadata tracker for the targeted host file descriptor.
 * @flags: Configuration execution metrics for specialized runtime environments.
 * @in_buf: Incoming data vector packet delivered over the channel boundary.
 * @in_bufsz: Total available length footprint of the input buffer payload.
 * @out_bufsz: Reserved buffer limit assigned by the calling system driver.
 *
 * Serves as the central multiplexing traffic cop for all arriving character
 * device IOCTL calls. Intercepts Linux storage and interface flags, checks
 * sequence lock states, and routes payload tasks cleanly down to dedicated
 * parser sub-modules.
 */
static void remote_i2c_cuse_ioctl(fuse_req_t req, int cmd, void *arg,
                         struct fuse_file_info *fi, unsigned flags,
                         const void *in_buf, size_t in_bufsz, size_t out_bufsz)
{
    RemoteI2CBackendCuse *cuse = fuse_req_userdata(req);
    unsigned int ctl = cmd;
    (void)fi;

    trace_remote_i2c_master_i2cdev_ioctl(cmd);

    /*
     * Guard against compatibility modes that violate
     * the 64-bit boundary model
     */
    if (flags & FUSE_IOCTL_COMPAT) {
        fuse_reply_err(req, ENOSYS);
        return;
    }

    if (cuse->ioctl_state == I2C_IOCTL_START) {
        cuse->last_ioctl = ctl;
    } else if (cuse->last_ioctl != ctl) {
        cuse->last_ioctl = 0;
        cuse->ioctl_state = I2C_IOCTL_START;
        fuse_reply_err(req, EINVAL);
        return;
    }

    switch (ctl) {
    case I2C_SLAVE_FORCE:
        /*
         * Mapped for compliance; force-lock requests are
         * acknowledged directly.
         */
        fuse_reply_ioctl(req, 0, NULL, 0);
        break;
    case I2C_FUNCS:
        remote_i2c_cuse_functional(cuse, req, arg, in_buf);
        break;
    case I2C_SLAVE:
        i2cdev_address(cuse, req, arg, in_buf);
        break;
    case I2C_SMBUS:
        remote_i2c_cuse_cmd_smbus(cuse, req, arg, in_buf, in_bufsz, out_bufsz);
        break;
    case I2C_RDWR:
        i2cdev_cmd_rdwr(cuse, req, arg, in_buf, in_bufsz, out_bufsz);
        break;
    default:
        fuse_reply_err(req, ENOTTY);
        break;
    }

    /* Normalize context states when a sub-transaction flow hits termination */
    if (cuse->ioctl_state == I2C_IOCTL_FINISHED) {
        cuse->ioctl_state = I2C_IOCTL_START;
        cuse->last_ioctl = 0;
        trace_remote_i2c_master_i2cdev_ioctl_finished(cmd);
    }
}

/*
 * remote_i2c_cuse_poll:
 * @req: The active FUSE request context handle.
 * @fi: Driver metadata tracker for the targeted host file descriptor.
 * @ph: Unused. I2C is master-initiated request-response; no async events.
 *
 * Reports the device as always ready for read and write, matching the
 * behaviour of the Linux kernel i2c-dev driver.
 */
static void remote_i2c_cuse_poll(fuse_req_t req, struct fuse_file_info *fi,
                        struct fuse_pollhandle *ph)
{
    (void)ph;
    fuse_reply_poll(req, POLL_IN | POLL_OUT);
}

static const struct cuse_lowlevel_ops i2cdev_ops = {
    .init       = remote_i2c_cuse_init,
    .open       = remote_i2c_cuse_open,
    .release    = remote_i2c_cuse_release,
    .read       = remote_i2c_cuse_read,
    .ioctl      = remote_i2c_cuse_ioctl,
    .poll       = remote_i2c_cuse_poll,
};

/*
 * remote_i2c_read_fuse_export:
 * @opaque: Dereferenced pointer targeting the concrete CUSE backend instance.
 *
 * Serves as the high-speed data pump registered into the QEMU main AioContext
 * loop. Constantly flushes the character node descriptor, intercepting
 * arriving kernel data blocks, processing loop structures, and preventing
 * context blocking.
 */
static void remote_i2c_read_fuse_export(void *opaque)
{
    RemoteI2CBackendCuse *cuse = opaque;
    int ret;

    do {
        ret = fuse_session_receive_buf(cuse->fuse_session, &cuse->fuse_buf);
    } while (ret == -EINTR);

    if (ret < 0) {
        return;
    }

    fuse_session_process_buf(cuse->fuse_session, &cuse->fuse_buf);
    trace_remote_i2c_master_fuse_io_read();
}

/*
 * remote_i2c_fuse_export:
 * @cuse: The concrete CUSE backend instance.
 * @errp: Pointer tracking system initialization error telemetry.
 *
 * Configures argument vector clusters, allocates native multi-threaded
 * system targets, builds character bindings via low-level libraries,
 * and maps the resulting FUSE descriptor natively into QEMU's primary
 * asynchronous loop handlers.
 */
int remote_i2c_fuse_export(RemoteI2CBackendCuse *cuse, Error **errp)
{
    GPtrArray *argv_ptr = g_ptr_array_new();
    char *curdir = get_current_dir_name();
    struct fuse_session *session = NULL;
    struct cuse_info ci = { 0 };
    char dev_name[128];
    int multithreaded;
    int ret;

    /* Build clean FUSE parameter vectors manually */
    g_ptr_array_add(argv_ptr, g_strdup("qemu-remote-i2c"));
    g_ptr_array_add(argv_ptr, g_strdup("-f"));
    g_ptr_array_add(argv_ptr, g_strdup("-s"));

    if (cuse->debug) {
        g_ptr_array_add(argv_ptr, g_strdup("-d"));
    }

    if (cuse->fuse_opts) {
        char **opts = g_strsplit(cuse->fuse_opts, " ", -1);
        for (int i = 0; opts[i] != NULL; i++) {
            if (opts[i][0] != '\0') {
                g_ptr_array_add(argv_ptr, g_strdup(opts[i]));
            }
        }
        g_strfreev(opts);
    }

    /* Prevent stack overrides; enforce size-bounded buffer formatting */
    snprintf(dev_name, sizeof(dev_name), "DEVNAME=%s", cuse->devname);
    const char *dev_info_argv[] = { dev_name };

    memset(&ci, 0, sizeof(ci));
    ci.dev_major = 0;
    ci.dev_minor = 0;
    ci.dev_info_argc = 1;
    ci.dev_info_argv = dev_info_argv;
    ci.flags = CUSE_UNRESTRICTED_IOCTL;

    session = cuse_lowlevel_setup(argv_ptr->len, (char **)argv_ptr->pdata,
                                  &ci, &i2cdev_ops, &multithreaded, cuse);

    g_ptr_array_set_free_func(argv_ptr, g_free);
    g_ptr_array_free(argv_ptr, TRUE);

    if (session == NULL) {
        error_setg(errp, "Remote I2C Backend: cuse_lowlevel_setup() failed");
        errno = EINVAL;
        return -1;
    }

    ret = chdir(curdir);
    if (ret == -1) {
        error_setg(errp,
                   "Remote I2C Backend: chdir() failed to restore root path");
        return -1;
    }

    /*
     * Link into QEMU's primary infrastructure loop for
     * clean data multiplexing
     */
    cuse->ctx = iohandler_get_aio_context();
    aio_set_fd_handler(cuse->ctx, fuse_session_fd(session),
                       remote_i2c_read_fuse_export, NULL,
                       NULL, NULL, cuse);
    cuse->fuse_session = session;

    trace_remote_i2c_master_fuse_export();
    return 0;
}

/*
 * remote_i2c_cuse_complete:
 * @uc: Raw object handle routing back into the UserCreatable QOM
 *      component interface.
 * @errp: Pointer tracking system initialization error telemetry.
 *
 * Implements the standard QOM post-instantiation lifecycle callback.
 * Asserts property constraint sanity checks before spinning up runtime
 * export workers.
 */
static void remote_i2c_cuse_complete(UserCreatable *uc, Error **errp)
{
    RemoteI2CBackendCuse *cuse = REMOTE_I2C_BACKEND_CUSE(uc);

    if (!cuse->devname) {
        error_setg(errp, "remote-i2c-backend-cuse requires 'devname' property");
        return;
    }

    if (remote_i2c_fuse_export(cuse, errp) < 0) {
        return;
    }
}

static void remote_i2c_cuse_class_init(ObjectClass *oc, const void *data)
{
    RemoteI2CBackendClass *bc = REMOTE_I2C_BACKEND_CLASS(oc);
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    bc->on_tx_complete = remote_i2c_cuse_on_tx_complete;
    bc->on_tx_error = remote_i2c_cuse_on_tx_error;

    ucc->complete = remote_i2c_cuse_complete;

    object_class_property_add_bool(oc, "debug",
                                   cuse_get_debug, cuse_set_debug);
    object_class_property_set_description(oc, "debug",
                                          "Enable debug logging for CUSE backend");

    object_class_property_add_str(oc, "devname",
                                  cuse_get_devname, cuse_set_devname);
    object_class_property_add_str(oc, "fuse-opts",
                                  cuse_get_fuse_opts,
                                  cuse_set_fuse_opts);
}

static const TypeInfo remote_i2c_cuse_info = {
    .name = TYPE_REMOTE_I2C_BACKEND_CUSE,
    .parent = TYPE_REMOTE_I2C_BACKEND,
    .instance_size = sizeof(RemoteI2CBackendCuse),
    .class_init = remote_i2c_cuse_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void remote_i2c_cuse_register_types(void)
{
    type_register_static(&remote_i2c_cuse_info);
}

type_init(remote_i2c_cuse_register_types)
