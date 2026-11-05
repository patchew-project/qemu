/*
 * QTest testcase for the INA230 current/voltage/power monitor
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

#define INA230_TEST_ID      "ina230-test"
#define INA230_TEST_ADDR    0x40

/* Register pointer addresses */
#define REG_CONFIG          0x00
#define REG_SHUNT           0x01
#define REG_BUS             0x02
#define REG_POWER           0x03
#define REG_CURRENT         0x04
#define REG_CALIBRATION     0x05
#define REG_MASK_ENABLE     0x06
#define REG_ALERT_LIMIT     0x07
#define REG_DIE_ID          0xFF

/* Configuration register bits */
#define CONFIG_POR          0x4127
#define CONFIG_RST          0x8000

/* Mask/Enable register bits */
#define ME_LEN              0x0001
#define ME_APOL             0x0002
#define ME_OVF              0x0004
#define ME_CVRF             0x0008
#define ME_AFF              0x0010
#define ME_CNVR             0x0400
#define ME_POL              0x0800
#define ME_BUL              0x1000
#define ME_BOL              0x2000
#define ME_SUL              0x4000
#define ME_SOL              0x8000

/* Injected-input scaling (datasheet fixed LSBs) */
#define SHUNT_LSB_NV        2500
#define BUS_LSB_UV          1250

#define DIE_ID_VAL          0x2310

/* QMP helpers for injecting the physical inputs */

static void qmp_ina230_set(const char *property, int value)
{
    QDict *resp;

    resp = qmp("{ 'execute': 'qom-set', 'arguments':"
               " { 'path': %s, 'property': %s, 'value': %d } }",
               INA230_TEST_ID, property, value);
    g_assert(qdict_haskey(resp, "return"));
    qobject_unref(resp);
}

static int qmp_ina230_get(const char *property)
{
    QDict *resp;
    int ret;

    resp = qmp("{ 'execute': 'qom-get', 'arguments':"
               " { 'path': %s, 'property': %s } }",
               INA230_TEST_ID, property);
    g_assert(qdict_haskey(resp, "return"));
    ret = qdict_get_int(resp, "return");
    qobject_unref(resp);
    return ret;
}

/* Power-on-reset default values and the Die ID */
static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get16(dev, REG_CONFIG), ==, CONFIG_POR);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_POWER), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_CALIBRATION), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_ALERT_LIMIT), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_DIE_ID), ==, DIE_ID_VAL);
}

/* Software reset via the Configuration RST bit */
static void test_soft_reset(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CALIBRATION, 0x1234);
    i2c_set16(dev, REG_MASK_ENABLE, ME_SOL);
    i2c_set16(dev, REG_ALERT_LIMIT, 0x0500);

    g_assert_cmphex(i2c_get16(dev, REG_CALIBRATION), ==, 0x1234);
    g_assert_cmphex(i2c_get16(dev, REG_ALERT_LIMIT), ==, 0x0500);

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    g_assert_cmphex(i2c_get16(dev, REG_CONFIG), ==, CONFIG_POR);
    g_assert_cmphex(i2c_get16(dev, REG_CALIBRATION), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_ALERT_LIMIT), ==, 0x0000);
}

/* Every bit except the self-clearing RST is writable and reads back */
static void test_config_wmask(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    /* Reserved bits are not forced: the guest value is retained */
    i2c_set16(dev, REG_CONFIG, 0x0007);
    g_assert_cmphex(i2c_get16(dev, REG_CONFIG), ==, 0x0007);

    i2c_set16(dev, REG_CONFIG, 0x7FFF);
    g_assert_cmphex(i2c_get16(dev, REG_CONFIG), ==, 0x7FFF);
}

/* Shunt voltage register, including two's-complement negatives */
static void test_shunt_injection(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    qmp_ina230_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x1F40);

    qmp_ina230_set("shunt-voltage", -80000000);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x8300);

    g_assert_cmpint(qmp_ina230_get("shunt-voltage"), ==, -80000000);
}

/* Bus voltage register */
static void test_bus_injection(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    qmp_ina230_set("bus-voltage", 12000000);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x2580);

    qmp_ina230_set("bus-voltage", 11980000);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x2570);
}

/* Calibration-derived current, using the datasheet worked example */
static void test_calibration_current(void *obj, void *data,
                                     QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina230_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x0000);

    i2c_set16(dev, REG_CALIBRATION, 0x0A00);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x2710);
}

/* Calibration-derived power, using the datasheet worked example */
static void test_power(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina230_set("shunt-voltage", 20000000);
    qmp_ina230_set("bus-voltage", 11980000);
    i2c_set16(dev, REG_CALIBRATION, 0x0A00);

    g_assert_cmphex(i2c_get16(dev, REG_POWER), ==, 0x12B8);
}

/* With Calibration == 0 the Current and Power registers stay at zero */
static void test_zero_cal(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina230_set("shunt-voltage", 20000000);
    qmp_ina230_set("bus-voltage", 11980000);

    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x0000);
    g_assert_cmphex(i2c_get16(dev, REG_POWER), ==, 0x0000);
}

/* Clamping at full scale and the OVF (math overflow) flag */
static void test_edges_overflow(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina230_set("shunt-voltage", 81917500);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x7FFF);
    qmp_ina230_set("shunt-voltage", -81920000);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x8000);

    qmp_ina230_set("bus-voltage", 36000000);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x7080);

    qmp_ina230_set("shunt-voltage", 20000000);
    i2c_set16(dev, REG_CALIBRATION, 0x7FFF);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x7FFF);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_OVF, ==, ME_OVF);
}

/* Shunt over-limit alert (transparent mode) sets the AFF flag */
static void test_alert_shunt_over(void *obj, void *data,
                                  QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    i2c_set16(dev, REG_ALERT_LIMIT, 0x1000);
    i2c_set16(dev, REG_MASK_ENABLE, ME_SOL);

    qmp_ina230_set("shunt-voltage", 2500000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);

    qmp_ina230_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, ME_AFF);

    qmp_ina230_set("shunt-voltage", 2500000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);
}

/* Latch mode holds the alert until the Mask/Enable register is read */
static void test_alert_latch(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    uint16_t me;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    i2c_set16(dev, REG_ALERT_LIMIT, 0x1000);
    i2c_set16(dev, REG_MASK_ENABLE, ME_SOL | ME_LEN);

    qmp_ina230_set("shunt-voltage", 20000000);
    qmp_ina230_set("shunt-voltage", 2500000);

    me = i2c_get16(dev, REG_MASK_ENABLE);
    g_assert_cmphex(me & ME_AFF, ==, ME_AFF);

    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);
}

/* Bus over-limit alert */
static void test_alert_bus(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    i2c_set16(dev, REG_ALERT_LIMIT, 0x2000);
    i2c_set16(dev, REG_MASK_ENABLE, ME_BOL);

    qmp_ina230_set("bus-voltage", 5000000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);

    qmp_ina230_set("bus-voltage", 12000000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, ME_AFF);
}

/* Power over-limit alert */
static void test_alert_power(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina230_set("shunt-voltage", 20000000);
    qmp_ina230_set("bus-voltage", 11980000);
    i2c_set16(dev, REG_CALIBRATION, 0x0A00);

    i2c_set16(dev, REG_ALERT_LIMIT, 0x1000);
    i2c_set16(dev, REG_MASK_ENABLE, ME_POL);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, ME_AFF);

    i2c_set16(dev, REG_ALERT_LIMIT, 0x2000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);
}

/* Conversion Ready Flag: set on conversion, cleared by reading Mask/Enable */
static void test_cvrf(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    /* A conversion (triggered by injection) sets CVRF */
    qmp_ina230_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_CVRF, ==, ME_CVRF);

    /* The previous read cleared CVRF; no conversion since, so it stays clear */
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_CVRF, ==, 0);
}

static void ina230_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" INA230_TEST_ID ",address=0x40"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { INA230_TEST_ADDR });

    qos_node_create_driver("ina230", i2c_device_create);
    qos_node_consumes("ina230", "i2c-bus", &opts);

    qos_add_test("defaults", "ina230", test_defaults, NULL);
    qos_add_test("soft-reset", "ina230", test_soft_reset, NULL);
    qos_add_test("config-wmask", "ina230", test_config_wmask, NULL);
    qos_add_test("shunt-injection", "ina230", test_shunt_injection, NULL);
    qos_add_test("bus-injection", "ina230", test_bus_injection, NULL);
    qos_add_test("calibration-current", "ina230", test_calibration_current,
                 NULL);
    qos_add_test("power", "ina230", test_power, NULL);
    qos_add_test("zero-cal", "ina230", test_zero_cal, NULL);
    qos_add_test("edges-overflow", "ina230", test_edges_overflow, NULL);
    qos_add_test("alert-shunt-over", "ina230", test_alert_shunt_over, NULL);
    qos_add_test("alert-latch", "ina230", test_alert_latch, NULL);
    qos_add_test("alert-bus", "ina230", test_alert_bus, NULL);
    qos_add_test("alert-power", "ina230", test_alert_power, NULL);
    qos_add_test("cvrf", "ina230", test_cvrf, NULL);
}
libqos_init(ina230_register_nodes);
