/*
 * I2C multiplexer for PCA954x series of I2C multiplexer/switch chips.
 *
 * Copyright 2021 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/i2c/i2c_mux_pca954x.h"
#include "hw/i2c/smbus_slave.h"
#include "hw/qdev-core.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/queue.h"
#include "trace.h"

int pca954x_add_child(I2CSlave *mux, uint8_t channel, I2CSlave *child)
{
    g_autofree char *name = NULL;
    /*
     * Ok, so we need to try to add the i2c_dev to channel for mux.
     * A channel can have multiple devices, we need a list for each channel.
     */
    Pca954xClass *pc = PCA954X_GET_CLASS(mux);
    Pca954xState *pca954x = PCA954X(mux);
    PcaMuxChild *controlled_device;

    /* Is the channel legal? */
    if (channel >= pc->nchans) {
        return -1;
    }

    controlled_device = g_new0(PcaMuxChild, 1);
    controlled_device->channel = channel;
    controlled_device->child = child;
    object_ref(OBJECT(controlled_device->child));

    /* Hide the device. */
    child->reachable = 0;

    QSLIST_INSERT_HEAD(&pca954x->children, controlled_device, sibling);

    name = g_strdup_printf("i2c@%u-child[%u]", channel,
                           pca954x->count[channel]);
    object_property_add_link(OBJECT(mux), name,
                             object_get_typename(OBJECT(child)),
                             (Object **)&controlled_device->child,
                             NULL, /* read-only property */
                             0);
    pca954x->count[channel]++;

    return 0;
}

static void pca954x_enable_channel(Pca954xState *s, uint8_t enable_mask)
{
    PcaMuxChild *kid;
    I2CSlave *child;

    /*
     * For each child, check if their bit is set in data and if yes, enable
     * them, otherwise disable, hide them.
     */
    QSLIST_FOREACH(kid, &s->children, sibling) {
        child = I2C_SLAVE(kid->child);
        if (enable_mask & (1 << kid->channel)) {
            child->reachable = true;
        } else {
            child->reachable = false;
        }
    }
}

static void pca954x_write(Pca954xState *s, uint8_t data)
{
    s->control = data;
    pca954x_enable_channel(s, data);

    trace_pca954x_write_bytes(data);
}

static int pca954x_write_data(SMBusDevice *d, uint8_t *buf, uint8_t len)
{
    Pca954xState *s = PCA954X(d);

    if (len == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: writing empty data\n", __func__);
        return -1;
    }

    /*
     * len should be 1, because they write one byte to enable/disable channels.
     */
    if (len > 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: extra data after channel selection mask\n",
            __func__);
        return -1;
    }

    pca954x_write(s, buf[0]);
    return 0;
}

static uint8_t pca954x_read_byte(SMBusDevice *d)
{
    Pca954xState *s = PCA954X(d);
    uint8_t data = s->control;
    trace_pca954x_read_data(data);
    return data;
}

static void pca9546_class_init(ObjectClass *oc, void *data)
{
    Pca954xClass *s = PCA954X_CLASS(oc);
    s->nchans = PCA9546_CHANNEL_COUNT;
}

static void pca9548_class_init(ObjectClass *oc, void *data)
{
    Pca954xClass *s = PCA954X_CLASS(oc);
    s->nchans = PCA9548_CHANNEL_COUNT;
}

static void pca954x_enter_reset(Object *obj, ResetType type)
{
    Pca954xState *s = PCA954X(obj);
    /* Reset will disable all channels. */
    pca954x_write(s, 0);
}

static void pca954x_init(Object *obj)
{
    Pca954xState *s = PCA954X(obj);
    memset(s->count, 0x00, sizeof(s->count));
}

static void pca954x_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *k = SMBUS_DEVICE_CLASS(klass);

    dc->desc = "Pca954x i2c-mux";
    k->write_data = pca954x_write_data;
    k->receive_byte = pca954x_read_byte;
    rc->phases.enter = pca954x_enter_reset;
}

static const TypeInfo pca954x_info[] = {
    {
        .name          = TYPE_PCA954X,
        .parent        = TYPE_SMBUS_DEVICE,
        .instance_size = sizeof(Pca954xState),
        .instance_init = pca954x_init,
        .class_size    = sizeof(Pca954xClass),
        .class_init    = pca954x_class_init,
        .abstract      = true,
    },
    {
        .name          = TYPE_PCA9546,
        .parent        = TYPE_PCA954X,
        .class_init    = pca9546_class_init,
    },
    {
        .name          = TYPE_PCA9548,
        .parent        = TYPE_PCA954X,
        .class_init    = pca9548_class_init,
    },
};

DEFINE_TYPES(pca954x_info)
