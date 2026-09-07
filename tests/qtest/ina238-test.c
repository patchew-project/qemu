/*
 * QTest testcase for the INA238 current/voltage/power monitor
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqos/i2c.h"
#include "libqos/qgraph.h"
#include "libqtest-single.h"
#include "qobject/qdict.h"

#define INA238_TEST_ID      "ina238-test"
#define INA238_TEST_ADDR    0x40

/* Register pointer addresses */
#define REG_CONFIG          0x00
#define REG_ADC_CONFIG      0x01
#define REG_SHUNT_CAL       0x02
#define REG_VSHUNT          0x04
#define REG_VBUS            0x05
#define REG_DIETEMP         0x06
#define REG_CURRENT         0x07
#define REG_POWER           0x08
#define REG_DIAG_ALRT       0x0B
#define REG_SOVL            0x0C
#define REG_SUVL            0x0D
#define REG_BOVL            0x0E
#define REG_BUVL            0x0F
#define REG_TEMP_LIMIT      0x10
#define REG_PWR_LIMIT       0x11
#define REG_MANUFACTURER_ID 0x3E
#define REG_DEVICE_ID       0x3F

/* Configuration register bits */
#define CONFIG_RST          0x8000
#define CONFIG_ADCRANGE     0x0010

/* DIAG_ALRT register bits */
#define DIAG_MEMSTAT        0x0001
#define DIAG_CNVRF          0x0002
#define DIAG_POL            0x0004
#define DIAG_BUSUL          0x0008
#define DIAG_BUSOL          0x0010
#define DIAG_SHNTUL         0x0020
#define DIAG_SHNTOL         0x0040
#define DIAG_TMPOL          0x0080
#define DIAG_MATHOF         0x0200
#define DIAG_APOL           0x1000
#define DIAG_SLOWALERT      0x2000
#define DIAG_CNVR           0x4000
#define DIAG_ALATCH         0x8000

/* Reset values */
#define CONFIG_POR          0x0000
#define ADC_CONFIG_POR      0xFB68
#define SHUNT_CAL_POR       0x1000
#define DIAG_ALRT_POR       0x0001
#define SOVL_POR            0x7FFF
#define SUVL_POR            0x8000
#define BOVL_POR            0x7FFF
#define BUVL_POR            0x0000
#define TEMP_LIMIT_POR      0x7FF0
#define PWR_LIMIT_POR       0xFFFF
#define MANUFACTURER_ID_VAL 0x5449
#define DEVICE_ID_VAL       0x2381

/* Read the 24-bit POWER register (MSB first) */
static uint32_t i2c_get24(QI2CDevice *dev, uint8_t reg)
{
    uint8_t resp[3];

    i2c_read_block(dev, reg, resp, sizeof(resp));
    return (resp[0] << 16) | (resp[1] << 8) | resp[2];
}

/* QMP helpers for injecting the physical inputs */

static void qmp_ina238_set(const char *property, int value)
{
    QDict *resp;

    resp = qmp("{ 'execute': 'qom-set', 'arguments':"
               " { 'path': %s, 'property': %s, 'value': %d } }",
               INA238_TEST_ID, property, value);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
}

static int qmp_ina238_get(const char *property)
{
    QDict *resp;
    int ret;

    resp = qmp("{ 'execute': 'qom-get', 'arguments':"
               " { 'path': %s, 'property': %s } }",
               INA238_TEST_ID, property);
    g_assert(qdict_haskey(resp, "return"));
    ret = qdict_get_int(resp, "return");
    qobject_unref(resp);
    return ret;
}

/* Power-on-reset default values and the ID registers */
static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get16(dev, REG_CONFIG), ==, CONFIG_POR);
    g_assert_cmphex(i2c_get16(dev, REG_ADC_CONFIG), ==, ADC_CONFIG_POR);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT_CAL), ==, SHUNT_CAL_POR);
    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_VBUS), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_DIETEMP), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x0000);
    g_assert_cmphex(i2c_get24(dev, REG_POWER), ==, 0x000000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT), ==, DIAG_ALRT_POR);
    g_assert_cmphex(i2c_get16(dev, REG_SOVL), ==, SOVL_POR);
    g_assert_cmphex(i2c_get16(dev, REG_SUVL), ==, SUVL_POR);
    g_assert_cmphex(i2c_get16(dev, REG_BOVL), ==, BOVL_POR);
    g_assert_cmphex(i2c_get16(dev, REG_BUVL), ==, BUVL_POR);
    g_assert_cmphex(i2c_get16(dev, REG_TEMP_LIMIT), ==, TEMP_LIMIT_POR);
    g_assert_cmphex(i2c_get16(dev, REG_PWR_LIMIT), ==, PWR_LIMIT_POR);
    g_assert_cmphex(i2c_get16(dev, REG_MANUFACTURER_ID), ==,
                    MANUFACTURER_ID_VAL);
    g_assert_cmphex(i2c_get16(dev, REG_DEVICE_ID), ==, DEVICE_ID_VAL);
}

/* Software reset via the Configuration RST bit */
static void test_soft_reset(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_SHUNT_CAL, 0x1234);
    i2c_set16(dev, REG_SOVL, 0x0500);
    i2c_set16(dev, REG_TEMP_LIMIT, 0x0A00);
    i2c_set16(dev, REG_DIAG_ALRT, DIAG_ALATCH);

    qmp_ina238_set("shunt-voltage", 97200000);
    qmp_ina238_set("bus-voltage", 48000000);
    qmp_ina238_set("die-temperature", 25000);

    g_assert_cmphex(i2c_get16(dev, REG_SHUNT_CAL), ==, 0x1234);
    g_assert_cmphex(i2c_get16(dev, REG_SOVL), ==, 0x0500);
    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0x4BF0);
    g_assert_cmpuint(i2c_get16(dev, REG_CURRENT), !=, 0);

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    g_assert_cmphex(i2c_get16(dev, REG_CONFIG), ==, CONFIG_POR);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT_CAL), ==, SHUNT_CAL_POR);
    g_assert_cmphex(i2c_get16(dev, REG_SOVL), ==, SOVL_POR);
    g_assert_cmphex(i2c_get16(dev, REG_TEMP_LIMIT), ==, TEMP_LIMIT_POR);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT), ==, DIAG_ALRT_POR);

    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_VBUS), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_DIETEMP), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x0000);
    g_assert_cmpuint(i2c_get24(dev, REG_POWER), ==, 0);
}

/* CONFIG keeps only the writable ADCRANGE/CONVDLY bits and self-clears RST */
static void test_config_wmask(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_ADCRANGE | 0x0040);
    g_assert_cmphex(i2c_get16(dev, REG_CONFIG), ==, CONFIG_ADCRANGE | 0x0040);

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    g_assert_cmphex(i2c_get16(dev, REG_CONFIG), ==, CONFIG_POR);
}

/* Shunt voltage register */
static void test_shunt_injection(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina238_set("shunt-voltage", 97200000);
    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0x4BF0);

    qmp_ina238_set("shunt-voltage", -80000000);
    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0xC180);

    g_assert_cmpint(qmp_ina238_get("shunt-voltage"), ==, -80000000);
}

/* Bus voltage register */
static void test_bus_injection(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina238_set("bus-voltage", 48000000);
    g_assert_cmphex(i2c_get16(dev, REG_VBUS), ==, 0x3C00);

    qmp_ina238_set("bus-voltage", 12000000);
    g_assert_cmphex(i2c_get16(dev, REG_VBUS), ==, 0x0F00);
}

/* Die temperature register */
static void test_temp_injection(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina238_set("die-temperature", 25000);
    g_assert_cmphex(i2c_get16(dev, REG_DIETEMP), ==, 0x0C80);

    qmp_ina238_set("die-temperature", -25000);
    g_assert_cmphex(i2c_get16(dev, REG_DIETEMP), ==, 0xF380);

    g_assert_cmpint(qmp_ina238_get("die-temperature"), ==, -25000);
}

/* Calibration-derived current */
static void test_calibration_current(void *obj, void *data,
                                     QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    qmp_ina238_set("shunt-voltage", 97200000);

    i2c_set16(dev, REG_SHUNT_CAL, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x0000);

    i2c_set16(dev, REG_SHUNT_CAL, 0x0FD2);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x4CCC);
}

/* Calibration-derived power */
static void test_power(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    qmp_ina238_set("shunt-voltage", 97200000);
    qmp_ina238_set("bus-voltage", 48000000);
    i2c_set16(dev, REG_SHUNT_CAL, 0x0FD2);

    g_assert_cmpuint(i2c_get24(dev, REG_POWER), ==, 4718400);
}

/* With SHUNT_CAL == 0 the current and power registers stay zero */
static void test_zero_cal(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_SHUNT_CAL, 0x0000);
    qmp_ina238_set("shunt-voltage", 97200000);
    qmp_ina238_set("bus-voltage", 48000000);

    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x0000);
    g_assert_cmpuint(i2c_get24(dev, REG_POWER), ==, 0);
}

/* ADCRANGE changes the shunt LSB, not the current divisor. */
static void test_adcrange(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina238_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0x0FA0);

    i2c_set16(dev, REG_SHUNT_CAL, 0x0800);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x1F40);

    i2c_set16(dev, REG_CONFIG, CONFIG_ADCRANGE);
    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0x3E80);

    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x7D00);

    i2c_set16(dev, REG_SHUNT_CAL, 0x2000);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x1F40);
}

/* Clamping at full scale and the MATHOF flag */
static void test_overflow(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    i2c_set16(dev, REG_CONFIG, CONFIG_ADCRANGE);
    qmp_ina238_set("shunt-voltage", 97200000);
    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0x7FFF);
    qmp_ina238_set("shunt-voltage", -97200000);
    g_assert_cmphex(i2c_get16(dev, REG_VSHUNT), ==, 0x8000);

    i2c_set16(dev, REG_CONFIG, CONFIG_POR);
    qmp_ina238_set("shunt-voltage", 163835000);
    i2c_set16(dev, REG_SHUNT_CAL, 0x0001);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x7FFF);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_MATHOF, ==,
                    DIAG_MATHOF);
}

/* Shunt over-limit alert */
static void test_alert_shunt(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_SOVL, 0x1000);

    qmp_ina238_set("shunt-voltage", 5000000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_SHNTOL, ==, 0);

    qmp_ina238_set("shunt-voltage", 97200000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_SHNTOL, ==,
                    DIAG_SHNTOL);

    qmp_ina238_set("shunt-voltage", 5000000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_SHNTOL, ==, 0);
}

/* Bus over-limit alert */
static void test_alert_bus(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_BOVL, 0x1000);

    qmp_ina238_set("bus-voltage", 12000000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_BUSOL, ==, 0);

    qmp_ina238_set("bus-voltage", 48000000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_BUSOL, ==, DIAG_BUSOL);
}

/* Over-temperature alert */
static void test_alert_temp(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_TEMP_LIMIT, 0x0A00);

    qmp_ina238_set("die-temperature", 10000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_TMPOL, ==, 0);

    qmp_ina238_set("die-temperature", 25000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_TMPOL, ==, DIAG_TMPOL);
}

/* Power over-limit alert */
static void test_alert_power(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    qmp_ina238_set("shunt-voltage", 97200000);
    qmp_ina238_set("bus-voltage", 48000000);
    i2c_set16(dev, REG_SHUNT_CAL, 0x0FD2);

    i2c_set16(dev, REG_PWR_LIMIT, 0x0100);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_POL, ==, DIAG_POL);

    i2c_set16(dev, REG_PWR_LIMIT, 0x7FFF);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_POL, ==, 0);
}

/* Two thresholds crossed at once are reported simultaneously (multi-alert) */
static void test_alert_multi(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t diag;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_SOVL, 0x1000);
    i2c_set16(dev, REG_BOVL, 0x1000);

    qmp_ina238_set("shunt-voltage", 97200000);
    qmp_ina238_set("bus-voltage", 48000000);

    diag = i2c_get16(dev, REG_DIAG_ALRT);
    g_assert_cmphex(diag & DIAG_SHNTOL, ==, DIAG_SHNTOL);
    g_assert_cmphex(diag & DIAG_BUSOL, ==, DIAG_BUSOL);
}

/* Latch mode holds the flag until the DIAG_ALRT register is read */
static void test_alert_latch(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_SOVL, 0x1000);
    i2c_set16(dev, REG_DIAG_ALRT, DIAG_ALATCH);

    qmp_ina238_set("shunt-voltage", 97200000);
    qmp_ina238_set("shunt-voltage", 5000000);

    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_SHNTOL, ==,
                    DIAG_SHNTOL);

    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_SHNTOL, ==, 0);
}

/* DIAG_ALRT control bits are writable; status bits are read-only */
static void test_diag_control(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t diag;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    i2c_set16(dev, REG_DIAG_ALRT,
              DIAG_ALATCH | DIAG_CNVR | DIAG_SLOWALERT | DIAG_APOL);
    diag = i2c_get16(dev, REG_DIAG_ALRT);
    g_assert_cmphex(diag & DIAG_ALATCH, ==, DIAG_ALATCH);
    g_assert_cmphex(diag & DIAG_CNVR, ==, DIAG_CNVR);
    g_assert_cmphex(diag & DIAG_SLOWALERT, ==, DIAG_SLOWALERT);
    g_assert_cmphex(diag & DIAG_APOL, ==, DIAG_APOL);
    /* MEMSTAT stays set */
    g_assert_cmphex(diag & DIAG_MEMSTAT, ==, DIAG_MEMSTAT);
}

/* Every DIAG_ALRT read clears CNVRF, whatever the alert-latch mode. */
static void test_cnvrf(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    qmp_ina238_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_CNVRF, ==, DIAG_CNVRF);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_CNVRF, ==, 0);

    i2c_set16(dev, REG_DIAG_ALRT, DIAG_ALATCH);
    qmp_ina238_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_CNVRF, ==, DIAG_CNVRF);
    g_assert_cmphex(i2c_get16(dev, REG_DIAG_ALRT) & DIAG_CNVRF, ==, 0);
}

static void ina238_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" INA238_TEST_ID ",address=0x40"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { INA238_TEST_ADDR });

    qos_node_create_driver("ina238", i2c_device_create);
    qos_node_consumes("ina238", "i2c-bus", &opts);

    qos_add_test("defaults", "ina238", test_defaults, NULL);
    qos_add_test("soft-reset", "ina238", test_soft_reset, NULL);
    qos_add_test("config-wmask", "ina238", test_config_wmask, NULL);
    qos_add_test("shunt-injection", "ina238", test_shunt_injection, NULL);
    qos_add_test("bus-injection", "ina238", test_bus_injection, NULL);
    qos_add_test("temp-injection", "ina238", test_temp_injection, NULL);
    qos_add_test("calibration-current", "ina238", test_calibration_current,
                 NULL);
    qos_add_test("power", "ina238", test_power, NULL);
    qos_add_test("zero-cal", "ina238", test_zero_cal, NULL);
    qos_add_test("adcrange", "ina238", test_adcrange, NULL);
    qos_add_test("overflow", "ina238", test_overflow, NULL);
    qos_add_test("alert-shunt", "ina238", test_alert_shunt, NULL);
    qos_add_test("alert-bus", "ina238", test_alert_bus, NULL);
    qos_add_test("alert-temp", "ina238", test_alert_temp, NULL);
    qos_add_test("alert-power", "ina238", test_alert_power, NULL);
    qos_add_test("alert-multi", "ina238", test_alert_multi, NULL);
    qos_add_test("alert-latch", "ina238", test_alert_latch, NULL);
    qos_add_test("diag-control", "ina238", test_diag_control, NULL);
    qos_add_test("cnvrf", "ina238", test_cnvrf, NULL);
}
libqos_init(ina238_register_nodes);
