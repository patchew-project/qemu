/*
 * Texas Instruments UCD90320 24-Rail PMBus Power Sequencer
 *
 * Copyright 2026 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_UCD90320 "ucd90320"

/* UCD90320 has 24 sequenced power-rail pages */
#define UCD90320_NUM_PAGES 24

/* Vendor-specific command codes (not in the standard PMBus register table) */
#define UCD9000_MONITOR_CONFIG  0xd5
#define UCD9000_NUM_PAGES       0xd6
#define UCD9000_MFR_STATUS      0xf3
#define UCD9000_DEVICE_ID       0xfd

typedef struct UCD90320State {
    PMBusDevice parent;
} UCD90320State;

static void ucd90320_send_block(PMBusDevice *pmdev,
                                const uint8_t *data, uint8_t len)
{
    int i;

    pmdev->out_buf[len + pmdev->out_buf_len] = len;
    for (i = len - 1; i >= 0; i--) {
        pmdev->out_buf[i + pmdev->out_buf_len] = data[len - 1 - i];
    }
    pmdev->out_buf_len += len + 1;
}

static uint8_t ucd90320_read_byte(PMBusDevice *pmdev)
{
    switch (pmdev->code) {
    case UCD9000_DEVICE_ID: {
        static const uint8_t id[] = "UCD90320";
        ucd90320_send_block(pmdev, id, sizeof(id) - 1);
        pmbus_idle(pmdev);
        return 0;
    }
    case UCD9000_NUM_PAGES:
        pmbus_send8(pmdev, UCD90320_NUM_PAGES);
        pmbus_idle(pmdev);
        return 0;

    case UCD9000_MONITOR_CONFIG: {
        static const uint8_t cfg[] = { 0x00 };
        ucd90320_send_block(pmdev, cfg, sizeof(cfg));
        pmbus_idle(pmdev);
        return 0;
    }
    case UCD9000_MFR_STATUS: {
        static const uint8_t status[] = { 0x00, 0x00, 0x00, 0x00 };
        ucd90320_send_block(pmdev, status, sizeof(status));
        pmbus_idle(pmdev);
        return 0;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: reading from unsupported register: 0x%02x\n",
                      __func__, pmdev->code);
        break;
    }
    return 0xFF;
}

static int ucd90320_write_data(PMBusDevice *pmdev, const uint8_t *buf,
                               uint8_t len)
{
    if (len == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: writing empty data\n", __func__);
        return -1;
    }

    pmdev->code = buf[0];

    if (len == 1) {
        return 0;
    }

    return 0;
}

static void ucd90320_exit_reset(Object *obj, ResetType type)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);

    pmdev->capability = 0x20; /* PEC supported */

    for (int i = 0; i < UCD90320_NUM_PAGES; i++) {
        pmdev->pages[i].operation     = 0x80; /* on */
        pmdev->pages[i].on_off_config = 0x1a;
        pmdev->pages[i].vout_mode     = 0x00; /* linear mode, exponent=0 */
        pmdev->pages[i].read_vout     = 0;    /* rails off, pgood=0 */
    }
}

static void ucd90320_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint64_t flags = PB_HAS_VOUT | PB_HAS_VOUT_MODE |
                     PB_HAS_STATUS_MFR_SPECIFIC;

    for (int i = 0; i < UCD90320_NUM_PAGES; i++) {
        pmbus_page_config(pmdev, i, flags);
    }
}

static void ucd90320_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);

    dc->desc = "Texas Instruments UCD90320 24-Rail Power Sequencer";
    k->write_data = ucd90320_write_data;
    k->receive_byte = ucd90320_read_byte;
    k->device_num_pages = UCD90320_NUM_PAGES;
    rc->phases.exit = ucd90320_exit_reset;
}

static const TypeInfo ucd90320_info = {
    .name          = TYPE_UCD90320,
    .parent        = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(UCD90320State),
    .instance_init = ucd90320_init,
    .class_init    = ucd90320_class_init,
};

static void ucd90320_register_types(void)
{
    type_register_static(&ucd90320_info);
}

type_init(ucd90320_register_types)
