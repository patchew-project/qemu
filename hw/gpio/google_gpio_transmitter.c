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

#include "migration/vmstate.h"
#include "hw/gpio/google_gpio_transmitter.h"
#include "hw/qdev-properties-system.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"
#include "qemu/log.h"

#define PACKET_REVISION 0x01

static bool google_gpio_tx_check_allowlist(GoogleGPIOTXState *s,
                                           uint32_t controller, uint32_t gpios)
{
    /* If the user didn't give us a list, allow everything */
    if (!s->gpio_state_by_ctlr) {
        return true;
    }

    GPIOCtlrState *gs = g_hash_table_lookup(s->gpio_state_by_ctlr, &controller);

    if (!gs) {
        return false;
    }

    bool updated = (gs->gpios & gs->allowed) != (gpios & gs->allowed);
    /* Update the new state */
    gs->gpios = gpios;

    return updated;
}

void google_gpio_tx_transmit(GoogleGPIOTXState *s, uint8_t controller,
                              uint32_t gpios)
{
    uint8_t packet[6];

    if (!google_gpio_tx_check_allowlist(s, controller, gpios)) {
        return;
    }

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

void google_gpio_tx_state_init(GoogleGPIOTXState *s, uint8_t controller,
                               uint32_t gpios)
{
    if (!s->gpio_state_by_ctlr) {
        return;
    }

    GPIOCtlrState *gs = g_hash_table_lookup(s->gpio_state_by_ctlr, &controller);
    if (gs) {
        gs->gpios = gpios;
    }
}

void google_gpio_tx_allowlist_qdev_init(GoogleGPIOTXState *s,
                                        const uint32_t *allowed_pins,
                                        size_t num)
{
    size_t i;
    char propname[64];

    qdev_prop_set_uint32(DEVICE(s), "len-gpio-allowlist", num);

    for (i = 0; i < num; i++) {
        snprintf(propname, sizeof(propname), "gpio-allowlist[%zu]", i);
        qdev_prop_set_uint32(DEVICE(s), propname, allowed_pins[i]);
    }
}

static void google_gpio_tx_allowlist_init(GoogleGPIOTXState *s)
{
    size_t i;
    GPIOCtlrState *gs;

    if (!s->gpio_allowlist) {
        return;
    }

    s->gpio_state_by_ctlr = g_hash_table_new_full(g_int_hash, g_int_equal,
                                                  g_free, g_free);

    for (i = 0; i < s->gpio_allowlist_sz; i++) {
        uint32_t controller = s->gpio_allowlist[i] / 32;
        uint32_t pin = (1 << (s->gpio_allowlist[i] % 32));

        gs = g_hash_table_lookup(s->gpio_state_by_ctlr, &controller);
        if (gs) {
            gs->allowed |= pin;
        } else {
            gs = g_malloc0(sizeof(*gs));
            gs->allowed |= pin;
            /*
             * The hash table relies on a pointer to be the key, so the pointer
             * containing the controller num must remain unchanged.
             * Because of that, just allocate a new key with the controller num.
             */
            uint32_t *ctlr = g_memdup(&controller, sizeof(controller));
            g_hash_table_insert(s->gpio_state_by_ctlr, ctlr, gs);
        }
    }
}

static void google_gpio_tx_realize(DeviceState *dev, Error **errp)
{
    GoogleGPIOTXState *s = GOOGLE_GPIO_TX(dev);

    google_gpio_tx_allowlist_init(s);

    qemu_chr_fe_set_handlers(&s->chr, google_gpio_tx_can_receive,
                             google_gpio_tx_receive,
                             google_gpio_tx_event,
                             NULL, OBJECT(s), NULL, true);
}

static void google_gpio_tx_finalize(Object *obj)
{
    GoogleGPIOTXState *s = GOOGLE_GPIO_TX(obj);

    g_hash_table_destroy(s->gpio_state_by_ctlr);
    g_free(s->gpio_allowlist);
}

static int google_gpio_tx_post_load(void *opaque, int version_id)
{
    GoogleGPIOTXState *s = GOOGLE_GPIO_TX(opaque);

    google_gpio_tx_allowlist_init(s);
    return 0;
}

static const VMStateDescription vmstate_google_gpio_tx = {
    .name = "gpio_transmitter",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = google_gpio_tx_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_VARRAY_UINT32(gpio_allowlist, GoogleGPIOTXState,
                              gpio_allowlist_sz, 0, vmstate_info_uint32,
                              uint32_t),
        VMSTATE_END_OF_LIST()
    }
};

static Property google_gpio_properties[] = {
    DEFINE_PROP_CHR("gpio-chardev", GoogleGPIOTXState, chr),
    DEFINE_PROP_ARRAY("gpio-allowlist", GoogleGPIOTXState, gpio_allowlist_sz,
                      gpio_allowlist, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_END_OF_LIST(),
};

static void google_gpio_tx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Google GPIO Controller Transmitter";
    dc->realize = google_gpio_tx_realize;
    dc->vmsd = &vmstate_google_gpio_tx;
    device_class_set_props(dc, google_gpio_properties);
}

static const TypeInfo google_gpio_tx_types[] = {
    {
        .name = TYPE_GOOGLE_GPIO_TRANSMITTER,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(GoogleGPIOTXState),
        .instance_finalize = google_gpio_tx_finalize,
        .class_init = google_gpio_tx_class_init,
    },
};

DEFINE_TYPES(google_gpio_tx_types);
