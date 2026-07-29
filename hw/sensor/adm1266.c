/*
 * Analog Devices ADM1266 Cascadable Super Sequencer with Margin Control and
 * Fault Recording with PMBus
 *
 * https://www.analog.com/media/en/technical-documentation/data-sheets/adm1266.pdf
 *
 * Copyright 2023 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_ADM1266 "adm1266"
OBJECT_DECLARE_SIMPLE_TYPE(ADM1266State, ADM1266)

#define ADM1266_BLACKBOX_CONFIG                 0xD3
#define ADM1266_PDIO_CONFIG                     0xD4
#define ADM1266_READ_STATE                      0xD9
#define ADM1266_READ_BLACKBOX                   0xDE
#define ADM1266_SET_RTC                         0xDF
#define ADM1266_GPIO_SYNC_CONFIGURATION         0xE1
#define ADM1266_BLACKBOX_INFORMATION            0xE6
#define ADM1266_PDIO_STATUS                     0xE9
#define ADM1266_GPIO_STATUS                     0xEA

/* Defaults */
#define ADM1266_OPERATION_DEFAULT               0x80
#define ADM1266_CAPABILITY_DEFAULT              0xA0
#define ADM1266_CAPABILITY_NO_PEC               0x20
#define ADM1266_PMBUS_REVISION_DEFAULT          0x22
#define ADM1266_MFR_ID_DEFAULT                  "ADI"
#define ADM1266_MFR_MODEL_DEFAULT               "ADM1266-A1"
#define ADM1266_MFR_REVISION_DEFAULT            "25"
#define ADM1266_MFR_LOCATION_DEFAULT            "0000"
#define ADM1266_MFR_DATE_DEFAULT                "0000"
#define ADM1266_MFR_SERIAL_DEFAULT              "0000"

#define ADM1266_NUM_PAGES                       17
#define ADM1266_READ_LENGTH_DEFAULT             48
/**
 * PAGE Index
 * Page 0 VH1.
 * Page 1 VH2.
 * Page 2 VH3.
 * Page 3 VH4.
 * Page 4 VP1.
 * Page 5 VP2.
 * Page 6 VP3.
 * Page 7 VP4.
 * Page 8 VP5.
 * Page 9 VP6.
 * Page 10 VP7.
 * Page 11 VP8.
 * Page 12 VP9.
 * Page 13 VP10.
 * Page 14 VP11.
 * Page 15 VP12.
 * Page 16 VP13.
 */
typedef struct ADM1266State {
    PMBusDevice parent;
    uint8_t read_length;

    char mfr_id[32];
    char mfr_model[32];
    char mfr_rev[8];
    char mfr_location[48];
    char mfr_date[16];
    char mfr_serial[32];
} ADM1266State;

static const uint8_t adm1266_ic_device_id[] = {0x03, 0x41, 0x12, 0x66};
static const uint8_t adm1266_ic_device_rev[] = {0x08, 0x01, 0x08, 0x07, 0x0,
                                                0x0, 0x07, 0x41, 0x30};

static void adm1266_exit_reset(Object *obj, ResetType type)
{
    ADM1266State *s = ADM1266(obj);
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);

    pmdev->page = 0;
    pmdev->capability = ADM1266_CAPABILITY_NO_PEC;

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        pmdev->pages[i].operation = ADM1266_OPERATION_DEFAULT;
        pmdev->pages[i].revision = ADM1266_PMBUS_REVISION_DEFAULT;
        pmdev->pages[i].vout_mode = 0;
        pmdev->pages[i].read_vout = pmbus_data2linear_mode(12, 0);
        pmdev->pages[i].vout_margin_high = pmbus_data2linear_mode(15, 0);
        pmdev->pages[i].vout_margin_low = pmbus_data2linear_mode(3, 0);
        pmdev->pages[i].vout_ov_fault_limit = pmbus_data2linear_mode(16, 0);
        pmdev->pages[i].revision = ADM1266_PMBUS_REVISION_DEFAULT;
    }

    memcpy(s->mfr_id, ADM1266_MFR_ID_DEFAULT, 4);
    memcpy(s->mfr_model, ADM1266_MFR_MODEL_DEFAULT, 11);
    memcpy(s->mfr_rev, ADM1266_MFR_REVISION_DEFAULT, 3);
    memcpy(s->mfr_location, ADM1266_MFR_LOCATION_DEFAULT, 5);
    memcpy(s->mfr_date, ADM1266_MFR_DATE_DEFAULT, 5);
    memcpy(s->mfr_serial, ADM1266_MFR_SERIAL_DEFAULT, 5);
    s->read_length = ADM1266_READ_LENGTH_DEFAULT;
}

static void adm1266_send_string(PMBusDevice *pmdev, const char *str)
{
    ADM1266State *s = ADM1266(pmdev);
    size_t len = strlen(str);

    if (s->read_length < len) {
        len = s->read_length;
    }

    g_assert(len + pmdev->out_buf_len < SMBUS_DATA_MAX_LEN);
    pmdev->out_buf[len + pmdev->out_buf_len] = len;

    for (int i = len - 1; i >= 0; i--) {
        pmdev->out_buf[i + pmdev->out_buf_len] = str[len - 1 - i];
    }
    pmdev->out_buf_len += len + 1;

    /* reset read length */
    s->read_length = ADM1266_READ_LENGTH_DEFAULT;
}

static uint8_t adm1266_read_byte(PMBusDevice *pmdev)
{
    ADM1266State *s = ADM1266(pmdev);

    switch (pmdev->code) {
    case PMBUS_MFR_ID:                    /* R/W block */
        adm1266_send_string(pmdev, s->mfr_id);
        break;

    case PMBUS_MFR_MODEL:                 /* R/W block */
        adm1266_send_string(pmdev, s->mfr_model);
        break;

    case PMBUS_MFR_REVISION:              /* R/W block */
        adm1266_send_string(pmdev, s->mfr_rev);
        break;

    case PMBUS_MFR_LOCATION:              /* R/W block */
        adm1266_send_string(pmdev, s->mfr_location);
        break;

    case PMBUS_MFR_DATE:                  /* R/W block */
        adm1266_send_string(pmdev, s->mfr_date);
        break;

    case PMBUS_MFR_SERIAL:                /* R/W block */
        adm1266_send_string(pmdev, s->mfr_serial);
        break;

    case PMBUS_IC_DEVICE_ID:
        pmbus_send(pmdev, adm1266_ic_device_id, sizeof(adm1266_ic_device_id));
        break;

    case PMBUS_IC_DEVICE_REV:
        pmbus_send(pmdev, adm1266_ic_device_rev, sizeof(adm1266_ic_device_rev));
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: reading from unimplemented register: 0x%02x\n",
                      __func__, pmdev->code);
        return 0xFF;
    }

    return 0;
}

static uint8_t adm1266_receive_block(PMBusDevice *pmdev, uint8_t *dest,
                                     size_t len)
{
    ADM1266State *s = ADM1266(pmdev);
    uint8_t sent_len;

    /* Exclude command code from return value */
    pmdev->in_buf++;
    pmdev->in_buf_len--;

    /* The byte after the command code denotes the length */
    sent_len = pmdev->in_buf[0];

    /* Block writes with length 1 are read requests */
    if (sent_len == 1) {
        s->read_length = pmdev->in_buf[1];
        return 0;
    }

    /* exclude length byte */
    pmdev->in_buf++;
    pmdev->in_buf_len--;

    /* Be as conservative as possible on how much data to receive */
    if (pmdev->in_buf_len < len) {
        len = pmdev->in_buf_len;
    }
    if (sent_len < len) {
        len = sent_len;
    }

    /* dest may contain data from previous writes */
    memset(dest, 0, len);
    memcpy(dest, pmdev->in_buf, len);
    return len;
}

static int adm1266_write_data(PMBusDevice *pmdev, const uint8_t *buf,
                              uint8_t len)
{
    ADM1266State *s = ADM1266(pmdev);

    switch (pmdev->code) {
    case PMBUS_MFR_ID:                    /* R/W block */
        adm1266_receive_block(pmdev, (uint8_t *)s->mfr_id, sizeof(s->mfr_id));
        break;

    case PMBUS_MFR_MODEL:                 /* R/W block */
        adm1266_receive_block(pmdev, (uint8_t *)s->mfr_model,
                              sizeof(s->mfr_model));
        break;

    case PMBUS_MFR_REVISION:               /* R/W block*/
        adm1266_receive_block(pmdev, (uint8_t *)s->mfr_rev, sizeof(s->mfr_rev));
        break;

    case PMBUS_MFR_LOCATION:               /* R/W block*/
        adm1266_receive_block(pmdev, (uint8_t *)s->mfr_location,
                              sizeof(s->mfr_location));
        break;

    case PMBUS_MFR_DATE:                   /* R/W block*/
        adm1266_receive_block(pmdev, (uint8_t *)s->mfr_date,
                              sizeof(s->mfr_date));
        break;

    case PMBUS_MFR_SERIAL:                 /* R/W block*/
        adm1266_receive_block(pmdev, (uint8_t *)s->mfr_serial,
                              sizeof(s->mfr_serial));
        break;

    case ADM1266_SET_RTC:   /* do nothing */
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: writing to unimplemented register: 0x%02x\n",
                      __func__, pmdev->code);
        break;
    }
    return 0;
}

static void adm1266_get(Object *obj, Visitor *v, const char *name, void *opaque,
                        Error **errp)
{
    uint32_t value, index;
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    PMBusVoutMode *mode = (PMBusVoutMode *)&pmdev->pages[0].vout_mode;

    if (strncmp(name, "vout[", 5) == 0) {
        sscanf(name, "vout[%u]", &index);
        mode = (PMBusVoutMode *)&pmdev->pages[index].vout_mode;
        value = pmbus_linear_mode2milliunits(*(uint16_t *)opaque, mode->exp);
    } else if (strncmp(name, "vout_mode", 9) == 0) {
        value = *(uint8_t *)opaque;
    } else {
        value = *(uint16_t *)opaque;
    }

    visit_type_uint32(v, name, &value, errp);
}

static void adm1266_set(Object *obj, Visitor *v, const char *name, void *opaque,
                        Error **errp)
{
    uint16_t *internal = opaque;
    uint32_t value, index;
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    PMBusVoutMode *mode;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (strncmp(name, "vout[", 5) == 0) {
        sscanf(name, "vout[%u]", &index);
        mode = (PMBusVoutMode *)&pmdev->pages[index].vout_mode;
        *internal = pmbus_milliunits2linear_mode(value, mode->exp);
    } else if (strncmp(name, "vout_mode", 9) == 0) {
        *(uint8_t *)opaque = value;
    } else {
        *internal = value;
    }
    pmbus_check_limits(pmdev);
}

static const VMStateDescription vmstate_adm1266 = {
    .name = "ADM1266",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]){
        VMSTATE_PMBUS_DEVICE(parent, ADM1266State),
        VMSTATE_END_OF_LIST()
    }
};

static void adm1266_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint64_t flags = PB_HAS_VOUT_MODE | PB_HAS_VOUT | PB_HAS_VOUT_MARGIN |
                     PB_HAS_VOUT_RATING;

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        pmbus_page_config(pmdev, i, flags);

        object_property_add(obj, "vout[*]", "uint32",
                            adm1266_get,
                            adm1266_set, NULL, &pmdev->pages[i].read_vout);

        object_property_add(obj, "vout_mode[*]", "uint32",
                            adm1266_get,
                            adm1266_set, NULL, &pmdev->pages[i].vout_mode);
    }
}

static void adm1266_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);

    dc->desc = "Analog Devices ADM1266 Hot Swap controller";
    dc->vmsd = &vmstate_adm1266;
    k->write_data = adm1266_write_data;
    k->receive_byte = adm1266_read_byte;
    k->device_num_pages = 17;

    rc->phases.exit = adm1266_exit_reset;
}

static const TypeInfo adm1266_info = {
    .name = TYPE_ADM1266,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(ADM1266State),
    .instance_init = adm1266_init,
    .class_init = adm1266_class_init,
};

static void adm1266_register_types(void)
{
    type_register_static(&adm1266_info);
}

type_init(adm1266_register_types)
