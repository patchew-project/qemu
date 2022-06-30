/*
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "hw/i2c/i2c.h"
#include "trace.h"

#define TYPE_INTEL_ME "intel-me"
OBJECT_DECLARE_SIMPLE_TYPE(IntelMEState, INTEL_ME);

struct IntelMEState {
    I2CSlave parent_obj;

    I2CBus *bus;
    QEMUBH *bh;
    int rx_len;
    int tx_len;
    int tx_pos;
    uint8_t rx_buf[512];
    uint8_t tx_buf[512];
};

static void intel_me_bh(void *opaque)
{
    IntelMEState *s = opaque;
    I2CSlave *i2c = I2C_SLAVE(s);
    uint8_t target_addr;

    assert(s->bus->bh == s->bh);

    switch (s->tx_pos) {
    case 0:
        target_addr = s->tx_buf[s->tx_pos++];
        trace_intel_me_tx_start(i2c->address, target_addr);
        if (i2c_start_send_async(s->bus, target_addr) != 0) {
            break;
        }
        return;
    default:
        if (s->tx_pos >= s->tx_len) {
            break;
        }
        trace_intel_me_tx_data(i2c->address, s->tx_buf[s->tx_pos]);
        if (i2c_send_async(s->bus, s->tx_buf[s->tx_pos++]) != 0) {
            break;
        }
        return;
    }

    trace_intel_me_tx_end(i2c->address);
    i2c_end_transfer(s->bus);
    i2c_bus_release(s->bus);
    s->tx_len = 0;
    s->tx_pos = 0;
    memset(s->tx_buf, 0, sizeof(s->tx_buf));
}

static void intel_me_realize(DeviceState *dev, Error **errp)
{
    IntelMEState *s = INTEL_ME(dev);

    s->bus = I2C_BUS(qdev_get_parent_bus(dev));
    s->bh = qemu_bh_new(intel_me_bh, s);
    s->rx_len = 0;
    s->tx_len = 0;
    s->tx_pos = 0;
    memset(s->rx_buf, 0, sizeof(s->rx_buf));
    memset(s->tx_buf, 0, sizeof(s->tx_buf));
}

static uint8_t checksum(const uint8_t *ptr, int len)
{
    int sum = 0;

    for (int i = 0; i < len; i++) {
        sum += ptr[i];
    }

    return 256 - sum;
}

static int intel_me_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    IntelMEState *s = INTEL_ME(i2c);

    switch (event) {
    case I2C_START_RECV:
        break;
    case I2C_START_SEND:
        trace_intel_me_rx_start(i2c->address);
        s->rx_len = 0;
        memset(s->rx_buf, 0, sizeof(s->rx_buf));
        break;
    case I2C_START_SEND_ASYNC:
        break;
    case I2C_FINISH:
        trace_intel_me_rx_end(i2c->address);
        s->tx_len = 10;
        s->tx_pos = 0;
        s->tx_buf[0] = s->rx_buf[2];
        s->tx_buf[1] = ((s->rx_buf[0] >> 2) + 1) << 2;
        s->tx_buf[2] = checksum(s->tx_buf, 2);
        s->tx_buf[3] = i2c->address;
        s->tx_buf[4] = (s->rx_buf[3] >> 2) << 2;
        s->tx_buf[5] = s->rx_buf[4];
        s->tx_buf[6] = 0x00;
        s->tx_buf[7] = 0x55;
        s->tx_buf[8] = 0x00;
        s->tx_buf[9] = checksum(s->tx_buf, s->tx_len - 1);
        s->tx_buf[0] >>= 1;
        i2c_bus_master(s->bus, s->bh);
        break;
    case I2C_NACK:
        break;
    }

    return 0;
}

static uint8_t intel_me_i2c_recv(I2CSlave *i2c)
{
    return 0xff;
}

static int intel_me_i2c_send(I2CSlave *i2c, uint8_t data)
{
    IntelMEState *s = INTEL_ME(i2c);

    trace_intel_me_rx_data(i2c->address, data);

    assert(s->rx_len < sizeof(s->rx_buf));
    s->rx_buf[s->rx_len++] = data;

    return 0;
}

static void intel_me_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *i2c = I2C_SLAVE_CLASS(oc);

    dc->realize = intel_me_realize;
    i2c->event = intel_me_i2c_event;
    i2c->recv = intel_me_i2c_recv;
    i2c->send = intel_me_i2c_send;
}

static const TypeInfo types[] = {
    {
        .name = TYPE_INTEL_ME,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(IntelMEState),
        .class_init = intel_me_class_init,
    },
};

DEFINE_TYPES(types);
