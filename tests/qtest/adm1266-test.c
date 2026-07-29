/*
 * Analog Devices ADM1266 Cascadable Super Sequencer with Margin Control and
 * Fault Recording with PMBus
 *
 * Copyright 2022 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <math.h>
#include "hw/i2c/pmbus_device.h"
#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "qobject/qdict.h"
#include "qobject/qnum.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"

#define TEST_ID "adm1266-test"
#define TEST_ADDR (0x12)

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
#define ADM1266_MFR_ID_DEFAULT_LEN              32
#define ADM1266_MFR_MODEL_DEFAULT               "ADM1266-A1"
#define ADM1266_MFR_MODEL_DEFAULT_LEN           32
#define ADM1266_MFR_REVISION_DEFAULT            "25"
#define ADM1266_MFR_REVISION_DEFAULT_LEN        8
#define TEST_STRING_A                           "a sample"
#define TEST_STRING_B                           "b sample"
#define TEST_STRING_C                           "rev c"

#define ADM1266_NUM_PAGES                       17
#define ADM1266_MAX_VALUE                       65535000

typedef union {
    uint8_t raw;
    PMBusVoutMode mode;
} ADM1266VoutMode;

static uint32_t qmp_adm1266_get(const char *id, const char *property)
{
    QDict *response;
    uint32_t ret;
    response = qmp("{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
                   "'property': %s } }", id, property);
    g_assert(qdict_haskey(response, "return"));
    ret = qnum_get_uint(qobject_to(QNum, qdict_get(response, "return")));
    qobject_unref(response);
    return ret;
}

static void qmp_adm1266_set(const char *id,
                             const char *property,
                             uint32_t value)
{
    QDict *response;

    response = qmp("{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
                   "'property': %s, 'value': %u } }",
                   id, property, value);
    g_assert(qdict_haskey(response, "return"));
}

static uint64_t adm1266_linear_mode2milliunits(uint16_t value, int exp)
{
    /* D = L * 2^e */
    uint64_t val = value;
    uint64_t ret;

    if (exp < 0) {
        ret = DIV_ROUND_CLOSEST((val * 1000), 1ULL << (-exp));
    } else {
        ret = (val << exp) * 1000;
    }

    if (ret > UINT32_MAX) {
        return UINT32_MAX;
    }

    return ret;
}

static void compare_string(QI2CDevice *i2cdev, uint8_t reg,
                           const char *test_str)
{
    uint8_t expected_len = strlen(test_str);
    uint8_t resp[SMBUS_DATA_MAX_LEN] = {0};

    g_assert(expected_len + 1 < SMBUS_DATA_MAX_LEN);
    i2c_read_block(i2cdev, reg, resp, expected_len + 1);
    g_assert_cmpint(resp[0], ==, expected_len);
    g_assert_cmpstr((char *)resp + 1, ==, test_str);
}

static void write_and_compare_string(QI2CDevice *i2cdev, uint8_t reg,
                                     const char *test_str, uint8_t len)
{
    char buf[SMBUS_DATA_MAX_LEN] = {0};
    buf[0] = len;
    strncpy(buf + 1, test_str, len);
    i2c_write_block(i2cdev, reg, (uint8_t *)buf, len + 1);
    compare_string(i2cdev, reg, test_str);
}

static void test_vout_milliunits(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value, value;
    uint64_t i2c_milliunits;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    char *path;
    ADM1266VoutMode m;

    /* set a different value in millivolts for each page */
    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        path = g_strdup_printf("vout[%d]", i);
        qmp_adm1266_set(TEST_ID, path, (1000 * (i + 1)));
    }

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        i2c_set8(i2cdev, PMBUS_PAGE, i);

        m.raw = i2c_get8(i2cdev, PMBUS_VOUT_MODE);
        i2c_value = bswap16(i2c_get16(i2cdev, PMBUS_READ_VOUT));
        i2c_milliunits = adm1266_linear_mode2milliunits(i2c_value, m.mode.exp);
        g_assert_cmpuint(i2c_milliunits, ==, (1000 * (i + 1)));

        path = g_strdup_printf("vout[%d]", i);
        value = qmp_adm1266_get(TEST_ID, path);
        g_assert_cmpuint(value, ==, (1000 * (i + 1)));
    }
}

/*
 * Note that the exponent determines the dynamic range, large exponents can not
 * be used with values that need to be incremented in small steps
 */
static void test_vout_mode_exponent(void *obj, void *data,
                                    QGuestAllocator *alloc)
{
    uint16_t i2c_value, value, expected;
    uint64_t i2c_milliunits;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    ADM1266VoutMode m;
    char *path;

    /* set a different exponent per page and a different value */
    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        i2c_set8(i2cdev, PMBUS_PAGE, i);
        expected = 1000 * (i * 2);
        m.mode.exp = i - 14;
        i2c_set8(i2cdev, PMBUS_VOUT_MODE, m.raw);
        path = g_strdup_printf("vout[%d]", i);
        qmp_adm1266_set(TEST_ID, path, expected);
    }

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        i2c_set8(i2cdev, PMBUS_PAGE, i);
        expected = 1000 * (i * 2);
        /* check correct value from i2c*/
        m.raw = i2c_get8(i2cdev, PMBUS_VOUT_MODE);
        i2c_value = bswap16(i2c_get16(i2cdev, PMBUS_READ_VOUT));
        i2c_milliunits = adm1266_linear_mode2milliunits(i2c_value, m.mode.exp);
        g_assert_cmpuint(i2c_milliunits, ==, expected);

        /* check correct value from qmp*/
        path = g_strdup_printf("vout[%d]", i);
        value = qmp_adm1266_get(TEST_ID, path);
        g_assert_cmpuint(value, ==, expected);
    }
}

static void test_vout_clamp_to_max(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    uint32_t value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    char *path;

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        path = g_strdup_printf("vout[%d]", i);
        qmp_adm1266_set(TEST_ID, path, 90000000);
    }

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        i2c_set8(i2cdev, PMBUS_PAGE, i);

        i2c_value = bswap16(i2c_get16(i2cdev, PMBUS_READ_VOUT));
        g_assert_cmpuint(i2c_value, ==, UINT16_MAX);

        path = g_strdup_printf("vout[%d]", i);
        value = qmp_adm1266_get(TEST_ID, path);
        g_assert_cmpuint(value, ==, ADM1266_MAX_VALUE);
    }
}

static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmphex(i2c_value, ==, ADM1266_OPERATION_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_REVISION);
    g_assert_cmphex(i2c_value, ==, ADM1266_PMBUS_REVISION_DEFAULT);

    compare_string(i2cdev, PMBUS_MFR_ID, ADM1266_MFR_ID_DEFAULT);
    compare_string(i2cdev, PMBUS_MFR_MODEL, ADM1266_MFR_MODEL_DEFAULT);
    compare_string(i2cdev, PMBUS_MFR_REVISION, ADM1266_MFR_REVISION_DEFAULT);
}

static void test_partial_reads(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    /* 1 byte block write requesting 7 byte response */
    uint8_t req_len[] = {0x01, 0x7};

    i2c_write_block(i2cdev, PMBUS_MFR_MODEL, req_len, sizeof(req_len));
    compare_string(i2cdev, PMBUS_MFR_MODEL, "ADM1266");

    req_len[1] = 0;
    i2c_write_block(i2cdev, PMBUS_MFR_MODEL, req_len, sizeof(req_len));
    compare_string(i2cdev, PMBUS_MFR_MODEL, "");

    req_len[1] = 100;
    i2c_write_block(i2cdev, PMBUS_MFR_MODEL, req_len, sizeof(req_len));
    compare_string(i2cdev, PMBUS_MFR_MODEL, ADM1266_MFR_MODEL_DEFAULT);
}

/* test r/w registers */
static void test_rw_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    /* test strings */
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_A,
                             sizeof(TEST_STRING_A));
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_B,
                             sizeof(TEST_STRING_B));
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_C,
                             sizeof(TEST_STRING_C));
}

static void adm1266_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" TEST_ID ",address=0x12"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { TEST_ADDR });

    qos_node_create_driver("adm1266", i2c_device_create);
    qos_node_consumes("adm1266", "i2c-bus", &opts);

    qos_add_test("test_defaults", "adm1266", test_defaults, NULL);
    qos_add_test("test_partial_reads", "adm1266", test_partial_reads, NULL);
    qos_add_test("test_rw_regs", "adm1266", test_rw_regs, NULL);
    qos_add_test("test_vout_milliunits", "adm1266",
                    test_vout_milliunits, NULL);
    qos_add_test("test_vout_mode_exponent", "adm1266",
                    test_vout_mode_exponent, NULL);
    qos_add_test("test_vout_clamp_to_max", "adm1266",
                    test_vout_clamp_to_max, NULL);
}

libqos_init(adm1266_register_nodes);
