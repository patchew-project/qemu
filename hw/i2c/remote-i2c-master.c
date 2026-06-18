// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Remote I2C Master
 *
 * This device exposes a QEMU I2C bus to the host system. Utilizing a decoupled
 * backend architecture (such as the CUSE backend), it allows external programs
 * or scripts on the host to interact with I2C slaves simulated inside QEMU as
 * if they were real hardware devices attached to the host.
 *
 * Features:
 * - Clean Frontend/Backend separation for transport-agnostic I2C emulation.
 * - Implements the Linux I2C ioctl interface (I2C_RDWR, I2C_SMBUS) via CUSE.
 * - Supports standard I2C and SMBus protocols.
 * - Handles asynchronous I2C transactions and clock stretching via QEMU
 *   Bottom Halves (BH).
 * - Configurable asynchronous slave timeouts (e.g., timeout-ms).
 * - Supports SMBus "Repeated Start" for atomic Write-then-Read operations.
 *
 * Usage:
 * Add the backend object and the master device to your QEMU command line:
 *
 *   -object remote-i2c-backend-cuse,id=my_cuse_backend,devname=i2c-33 \
 *   -device remote-i2c-master,i2cbus=/machine/soc[0]/i2c[0]/i2c,\
 *           backend=my_cuse_backend,timeout-ms=8000
 *
 * This creates a character device at /dev/i2c-33 on the host.
 * Use standard tools (i2c-tools) or C programs to access it:
 *
 *   i2cdetect -y -l
 *   i2cget -y <bus_id> <addr> <reg>
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

#include "hw/i2c/remote-i2c-master.h"

static void remote_i2c_timer_cb(void *opaque)
{
    RemoteI2CMasterState *s = opaque;

    if (s->backend->bus_state == I2C_BUS_WAIT_STRETCH) {
        trace_remote_i2c_master_timeout(s->backend->timeout_ms);
        s->backend->timed_out = true;
        qemu_bh_schedule(s->bh);
    } else if (s->backend->bus_state != I2C_BUS_IDLE &&
               s->backend->bus_state != I2C_BUS_WAIT_STRETCH) {
        /* Retry logic */
        timer_mod(s->timer_start_transmit,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 50000);
    }
}

static void remote_i2c_timer_step_cb(void *opaque)
{
    RemoteI2CMasterState *s = opaque;
    qemu_bh_schedule(s->bh);
}

static void remote_i2c_master_realize(DeviceState *dev, Error **errp)
{
    RemoteI2CMasterState *s = REMOTE_I2C_MASTER(dev);

    if (!s->backend) {
        error_setg(errp, "remote-i2c-master requires a 'backend' property");
        return;
    }

    s->backend->frontend = s;
    s->backend->timeout_ms = s->timeout_ms;

    s->bh = aio_bh_new_guarded(qemu_get_aio_context(),
                               remote_i2c_fsm_bh,
                               s,
                               &dev->mem_reentrancy_guard);

    s->timer = timer_new(QEMU_CLOCK_VIRTUAL, SCALE_MS,
                         &remote_i2c_timer_cb, s);
    s->timer_start_transmit = timer_new(QEMU_CLOCK_VIRTUAL,
                                        SCALE_NS,
                                        &remote_i2c_fsm_timer_start_transmit_cb,
                                        s);
    s->timer_step = timer_new(QEMU_CLOCK_VIRTUAL, SCALE_NS,
                              &remote_i2c_timer_step_cb, s);
}

static void remote_i2c_master_reset(Object *obj, ResetType type)
{
    RemoteI2CMasterState *s = REMOTE_I2C_MASTER(obj);

    /* remote_i2c_fsm_dispatch is defined in remote-i2c-fsm.c */
    remote_i2c_fsm_dispatch(s, REMOTE_I2C_CMD_RESET);
}

static const Property remote_i2c_master_properties[] = {
    DEFINE_PROP_LINK("i2cbus", RemoteI2CMasterState, bus,
                     TYPE_I2C_BUS, I2CBus *),
    DEFINE_PROP_STRING("name", RemoteI2CMasterState, name),
    DEFINE_PROP_LINK("backend", RemoteI2CMasterState, backend,
                     TYPE_REMOTE_I2C_BACKEND, RemoteI2CBackend *),
    DEFINE_PROP_BOOL("raise_arbitrage_lost",
                     RemoteI2CMasterState,
                     raise_arbitrage_lost, false),
    DEFINE_PROP_UINT32("timeout-ms",
                       RemoteI2CMasterState, timeout_ms, 1000),
};

static void remote_i2c_master_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, remote_i2c_master_properties);
    dc->realize = remote_i2c_master_realize;
    dc->desc = "Remote I2C Controller";
    rc->phases.enter = remote_i2c_master_reset;
}

static const TypeInfo remote_i2c_master_info = {
    .name          = TYPE_REMOTE_I2C_MASTER,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RemoteI2CMasterState),
    .class_init    = remote_i2c_master_class_init,
};

static void remote_i2c_master_register_types(void)
{
    type_register_static(&remote_i2c_master_info);
}

type_init(remote_i2c_master_register_types)
