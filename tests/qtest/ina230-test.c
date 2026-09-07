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

/* Injection-property limits (see ina230.c) */
#define SHUNT_MIN_NV        (-81920000)
#define SHUNT_MAX_NV        81917500
#define BUS_MAX_UV          36000000

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

/* A qom-set expected to be rejected (value outside the supported range) */
static void qmp_ina230_set_fail(const char *property, int64_t value)
{
    QDict *resp;

    resp = qmp("{ 'execute': 'qom-set', 'arguments':"
               " { 'path': %s, 'property': %s, 'value': %lld } }",
               INA230_TEST_ID, property, (long long)value);
    g_assert(qdict_haskey(resp, "error"));
    qobject_unref(resp);
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

/* Operating modes */
static void test_modes(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;
    static const struct {
        uint8_t mode;
        uint16_t shunt, bus, current, power;
    } cases[] = {
        /* Power-down: no conversion, every register retains the baseline */
        { 0x0, 0x1F40, 0x2570, 0x2710, 0x12B8 },
        { 0x4, 0x1F40, 0x2570, 0x2710, 0x12B8 },
        /* Shunt only (continuous): shunt and current track the probe */
        { 0x5, 0x0FA0, 0x2570, 0x1388, 0x12B8 },
        /* Shunt and bus (continuous): every register tracks the probe */
        { 0x7, 0x0FA0, 0x12C0, 0x1388, 0x04B0 },
    };
    size_t idx;

    for (idx = 0; idx < ARRAY_SIZE(cases); idx++) {
        i2c_set16(dev, REG_CONFIG, CONFIG_RST);
        i2c_set16(dev, REG_CALIBRATION, 0x0A00);
        qmp_ina230_set("shunt-voltage", 20000000);
        qmp_ina230_set("bus-voltage", 11980000);

        i2c_set16(dev, REG_CONFIG, cases[idx].mode);
        qmp_ina230_set("shunt-voltage", 10000000);
        qmp_ina230_set("bus-voltage", 6000000);

        g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, cases[idx].shunt);
        g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, cases[idx].bus);
        g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, cases[idx].current);
        g_assert_cmphex(i2c_get16(dev, REG_POWER), ==, cases[idx].power);
    }
}

/*
 * Triggered modes perform exactly one conversion per trigger, where the
 * trigger is a Configuration-register write. Injected input changes
 * between triggers are ignored until the next write.
 */
static void test_triggered(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_CALIBRATION, 0x0A00);

    qmp_ina230_set("shunt-voltage", 20000000);
    qmp_ina230_set("bus-voltage", 11980000);

    i2c_set16(dev, REG_CONFIG, 0x03);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x1F40);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x2570);

    qmp_ina230_set("shunt-voltage", 10000000);
    qmp_ina230_set("bus-voltage", 6000000);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x1F40);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x2570);

    i2c_set16(dev, REG_CONFIG, 0x03);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x0FA0);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x12C0);

    i2c_set16(dev, REG_CONFIG, 0x07);
    qmp_ina230_set("shunt-voltage", 20000000); /* A */
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x1F40);
}

/*
 * In a bus-only mode the current register is retained, but the power register
 * is still refreshed on each bus conversion from that retained current.
 */
static void test_bus_only_power(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_CALIBRATION, 0x0A00);
    qmp_ina230_set("shunt-voltage", 20000000);
    qmp_ina230_set("bus-voltage", 11980000);
    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x2710);

    i2c_set16(dev, REG_CONFIG, 0x06);
    qmp_ina230_set("bus-voltage", 6000000);

    g_assert_cmphex(i2c_get16(dev, REG_CURRENT), ==, 0x2710);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x12C0);
    g_assert_cmphex(i2c_get16(dev, REG_POWER), ==, 0x0960);
}

/* Shunt voltage register */
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
static void test_alert_latch_persist(void *obj, void *data,
                                     QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_ALERT_LIMIT, 0x1000);
    i2c_set16(dev, REG_MASK_ENABLE, ME_SOL | ME_LEN);

    qmp_ina230_set("shunt-voltage", 20000000);

    /* First read shows the latched AFF and clears the latch */
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, ME_AFF);

    /* Fault still present but no new conversion: AFF stays cleared */
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);

    qmp_ina230_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, ME_AFF);
}

/* A Configuration write releases the latch as a Mask/Enable read does. */
static void test_alert_latch_config(void *obj, void *data,
                                    QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_ALERT_LIMIT, 0x1000);
    i2c_set16(dev, REG_MASK_ENABLE, ME_SOL | ME_LEN);

    /* Latch a fault, then bring the input back below the limit */
    qmp_ina230_set("shunt-voltage", 20000000);
    qmp_ina230_set("shunt-voltage", 2500000);

    i2c_set16(dev, REG_CONFIG, CONFIG_POR);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);

    /* With the fault present, the reconversion latches it again */
    qmp_ina230_set("shunt-voltage", 20000000);
    i2c_set16(dev, REG_CONFIG, CONFIG_POR);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, ME_AFF);
}

/* The releasing write need not reconvert: a power-down leaves it clear. */
static void test_alert_latch_shutdown(void *obj, void *data,
                                      QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_ALERT_LIMIT, 0x1000);
    i2c_set16(dev, REG_MASK_ENABLE, ME_SOL | ME_LEN);

    qmp_ina230_set("shunt-voltage", 20000000);

    i2c_set16(dev, REG_CONFIG, 0x0000); /* power-down */
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);
}

/*
 * A conversion only re-samples the alert of a channel it converted: the bus
 * register a shunt-only conversion left stale must not re-latch.
 */
static void test_alert_channel(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);
    i2c_set16(dev, REG_ALERT_LIMIT, 0x2000);
    i2c_set16(dev, REG_MASK_ENABLE, ME_BOL | ME_LEN);

    qmp_ina230_set("bus-voltage", 12000000);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, ME_AFF);

    /* Shunt-only mode: the bus register keeps its over-limit value */
    i2c_set16(dev, REG_CONFIG, 0x0005);
    qmp_ina230_set("shunt-voltage", 20000000);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x2580);
    g_assert_cmphex(i2c_get16(dev, REG_MASK_ENABLE) & ME_AFF, ==, 0);
}

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

/* Shunt-voltage injection limits */
static void test_shunt_limits(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina230_set("shunt-voltage", SHUNT_MAX_NV);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x7FFF);
    g_assert_cmpint(qmp_ina230_get("shunt-voltage"), ==, SHUNT_MAX_NV);

    qmp_ina230_set("shunt-voltage", SHUNT_MIN_NV);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x8000);
    g_assert_cmpint(qmp_ina230_get("shunt-voltage"), ==, SHUNT_MIN_NV);

    qmp_ina230_set_fail("shunt-voltage", (int64_t)SHUNT_MAX_NV + 1);
    qmp_ina230_set_fail("shunt-voltage", (int64_t)SHUNT_MIN_NV - 1);
    g_assert_cmpint(qmp_ina230_get("shunt-voltage"), ==, SHUNT_MIN_NV);
    g_assert_cmphex(i2c_get16(dev, REG_SHUNT), ==, 0x8000);
}

/* Bus-voltage injection limits. */
static void test_bus_limits(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *dev = (QI2CDevice *)obj;

    i2c_set16(dev, REG_CONFIG, CONFIG_RST);

    qmp_ina230_set("bus-voltage", 0);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x0000);

    qmp_ina230_set("bus-voltage", BUS_MAX_UV);
    g_assert_cmphex(i2c_get16(dev, REG_BUS), ==, 0x7080);
    g_assert_cmpint(qmp_ina230_get("bus-voltage"), ==, BUS_MAX_UV);

    qmp_ina230_set_fail("bus-voltage", (int64_t)BUS_MAX_UV + 1);
    qmp_ina230_set_fail("bus-voltage", -1);
    g_assert_cmpint(qmp_ina230_get("bus-voltage"), ==, BUS_MAX_UV);
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
    qos_add_test("modes", "ina230", test_modes, NULL);
    qos_add_test("triggered", "ina230", test_triggered, NULL);
    qos_add_test("bus-only-power", "ina230", test_bus_only_power, NULL);
    qos_add_test("shunt-injection", "ina230", test_shunt_injection, NULL);
    qos_add_test("bus-injection", "ina230", test_bus_injection, NULL);
    qos_add_test("shunt-limits", "ina230", test_shunt_limits, NULL);
    qos_add_test("bus-limits", "ina230", test_bus_limits, NULL);
    qos_add_test("calibration-current", "ina230", test_calibration_current,
                 NULL);
    qos_add_test("power", "ina230", test_power, NULL);
    qos_add_test("zero-cal", "ina230", test_zero_cal, NULL);
    qos_add_test("edges-overflow", "ina230", test_edges_overflow, NULL);
    qos_add_test("alert-shunt-over", "ina230", test_alert_shunt_over, NULL);
    qos_add_test("alert-latch", "ina230", test_alert_latch, NULL);
    qos_add_test("alert-latch-persist", "ina230", test_alert_latch_persist,
                 NULL);
    qos_add_test("alert-latch-config", "ina230", test_alert_latch_config,
                 NULL);
    qos_add_test("alert-latch-shutdown", "ina230", test_alert_latch_shutdown,
                 NULL);
    qos_add_test("alert-channel", "ina230", test_alert_channel, NULL);
    qos_add_test("alert-bus", "ina230", test_alert_bus, NULL);
    qos_add_test("alert-power", "ina230", test_alert_power, NULL);
    qos_add_test("cvrf", "ina230", test_cvrf, NULL);
}
libqos_init(ina230_register_nodes);
