/*
 * Google GPIO Transmitter.
 *
 * This is a fake hardware model that does not exist on any board or IC.
 * The purpose of this model is to aggregate GPIO state changes from a GPIO
 * controller and transmit them via chardev.
 *
 * Copyright 2021 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"

#include "hw/gpio/google_gpio_transmitter.h"
#include "hw/qdev-properties-system.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"
#include "qemu/log.h"

#define PACKET_REVISION 0x01

void google_gpio_tx_transmit(GoogleGPIOTXState *s, uint8_t controller,
                              uint32_t gpios)
{
    uint8_t packet[6];

    packet[0] = PACKET_REVISION;
    packet[1] = controller;
    memcpy(&packet[2], &gpios, sizeof(gpios));
    qemu_chr_fe_write_all(&s->chr, packet, ARRAY_SIZE(packet));
}

static void google_gpio_tx_event(void *opaque, QEMUChrEvent evt)
{
    switch (evt) {
    case CHR_EVENT_OPENED:
    case CHR_EVENT_CLOSED:
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /*
         * Ignore events.
         * Our behavior stays the same regardless of what happens.
         */
        break;
    default:
        g_assert_not_reached();
    }
}

static void google_gpio_tx_receive(void *opaque, const uint8_t *buf, int size)
{
    GoogleGPIOTXState *s = GOOGLE_GPIO_TX(opaque);

    switch (buf[0]) {
    case GPIOTXCODE_OK:
        break;
    case GPIOTXCODE_MALFORMED_PKT:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Transmitted malformed packet\n",
                      object_get_canonical_path(OBJECT(s)));
        break;
    case GPIOTXCODE_UNKNOWN_VERSION:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Transmitted malformed packet "
                      "with a version the recipent can't handle. Sent "
                      "version %d\n", object_get_canonical_path(OBJECT(s)),
                      PACKET_REVISION);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown response 0x%x\n",
                      object_get_canonical_path(OBJECT(s)), buf[0]);
        break;
    }

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Expects packets only of length 1\n",
                      object_get_canonical_path(OBJECT(s)));
    }
}

static int google_gpio_tx_can_receive(void *opaque)
{
    return 1;
}

static void google_gpio_tx_realize(DeviceState *dev, Error **errp)
{
    GoogleGPIOTXState *s = GOOGLE_GPIO_TX(dev);

    qemu_chr_fe_set_handlers(&s->chr, google_gpio_tx_can_receive,
                             google_gpio_tx_receive,
                             google_gpio_tx_event,
                             NULL, OBJECT(s), NULL, true);
}

static Property google_gpio_properties[] = {
    DEFINE_PROP_CHR("gpio-chardev", GoogleGPIOTXState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void google_gpio_tx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Google GPIO Controller Transmitter";
    dc->realize = google_gpio_tx_realize;
    device_class_set_props(dc, google_gpio_properties);
}

static const TypeInfo google_gpio_tx_types[] = {
    {
        .name = TYPE_GOOGLE_GPIO_TRANSMITTER,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(GoogleGPIOTXState),
        .class_init = google_gpio_tx_class_init,
    },
};

DEFINE_TYPES(google_gpio_tx_types);
