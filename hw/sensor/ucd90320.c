/*
 * Texas Instruments UCD90320 24-Rail PMBus Power Sequencer
 *
 * Copyright 2026 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "migration/vmstate.h"
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

#define UCD90320_DEVICE_ID_LEN     8
#define UCD90320_MFR_STATUS_LEN    4

typedef struct UCD90320State {
    PMBusDevice parent;

    /* Reset values for the vendor-specific block-read commands. */
    uint8_t device_id[UCD90320_DEVICE_ID_LEN];
    uint8_t monitor_config;
    uint8_t mfr_status[UCD90320_MFR_STATUS_LEN];
} UCD90320State;

#define UCD90320(obj) OBJECT_CHECK(UCD90320State, (obj), TYPE_UCD90320)

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
    UCD90320State *s = UCD90320(pmdev);

    switch (pmdev->code) {
    case UCD9000_DEVICE_ID:
        ucd90320_send_block(pmdev, s->device_id, sizeof(s->device_id));
        pmbus_idle(pmdev);
        return 0;
    case UCD9000_NUM_PAGES:
        pmbus_send8(pmdev, UCD90320_NUM_PAGES);
        pmbus_idle(pmdev);
        return 0;

    case UCD9000_MONITOR_CONFIG:
        ucd90320_send_block(pmdev, &s->monitor_config,
                            sizeof(s->monitor_config));
        pmbus_idle(pmdev);
        return 0;
    case UCD9000_MFR_STATUS:
        ucd90320_send_block(pmdev, s->mfr_status, sizeof(s->mfr_status));
        pmbus_idle(pmdev);
        return 0;
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
    UCD90320State *s = UCD90320(obj);

    pmdev->capability = 0x20; /* PEC supported */

    for (int i = 0; i < UCD90320_NUM_PAGES; i++) {
        pmdev->pages[i].operation     = 0x80; /* on */
        pmdev->pages[i].on_off_config = 0x1a;
        pmdev->pages[i].vout_mode     = 0x00; /* linear mode, exponent=0 */
        pmdev->pages[i].read_vout     = 0;    /* rails off, pgood=0 */
    }

    memcpy(s->device_id, "UCD90320", sizeof(s->device_id));
    s->monitor_config = 0x00;
    memset(s->mfr_status, 0x00, sizeof(s->mfr_status));
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

static const VMStateDescription vmstate_ucd90320 = {
    .name = TYPE_UCD90320,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_PMBUS_DEVICE(parent, UCD90320State),
        VMSTATE_UINT8_ARRAY(device_id, UCD90320State, UCD90320_DEVICE_ID_LEN),
        VMSTATE_UINT8(monitor_config, UCD90320State),
        VMSTATE_UINT8_ARRAY(mfr_status, UCD90320State,
                            UCD90320_MFR_STATUS_LEN),
        VMSTATE_END_OF_LIST()
    }
};

static void ucd90320_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);

    dc->desc = "Texas Instruments UCD90320 24-Rail Power Sequencer";
    dc->vmsd = &vmstate_ucd90320;
    k->write_data = ucd90320_write_data;
    k->receive_byte = ucd90320_read_byte;
    k->device_num_pages = UCD90320_NUM_PAGES;
    rc->phases.exit = ucd90320_exit_reset;
}

static const TypeInfo ucd90320_types[] = {
    {
        .name          = TYPE_UCD90320,
        .parent        = TYPE_PMBUS_DEVICE,
        .instance_size = sizeof(UCD90320State),
        .instance_init = ucd90320_init,
        .class_init    = ucd90320_class_init,
    },
};

DEFINE_TYPES(ucd90320_types)
