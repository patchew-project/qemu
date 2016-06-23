/*
 * QEMU boot measurement
 *
 * Copyright (c) 2016 CoreOS, Inc <mjg59@coreos.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "hw/isa/isa.h"
#include "crypto/hash.h"
#include "hw/misc/measurements.h"
#include "monitor/monitor.h"
#include "hw/loader.h"

#define MEASUREMENT(obj) OBJECT_CHECK(MeasurementState, (obj), TYPE_MEASUREMENTS)

typedef struct MeasurementState MeasurementState;

struct MeasurementState {
    ISADevice parent_obj;
    MemoryRegion io_select;
    MemoryRegion io_value;
    uint16_t iobase;
    uint8_t measurements[24][20];
    uint8_t tmpmeasurement[20];
    int write_count;
    int read_count;
    uint8_t pcr;
};

static void measurement_reset(DeviceState *dev)
{
    MeasurementState *s = MEASUREMENT(dev);

    memset(s->measurements, 0, sizeof(s->measurements));
    measure_roms();
}

static void measurement_select(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MeasurementState *s = MEASUREMENT(opaque);
    s->pcr = val;
    s->read_count = 0;
    s->write_count = 0;
}

static uint64_t measurement_version(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static uint64_t measurement_read(void *opaque, hwaddr addr, unsigned size)
{
    MeasurementState *s = MEASUREMENT(opaque);

    if (s->read_count == 20) {
        s->read_count = 0;
    }
    return s->measurements[s->pcr][s->read_count++];
}

static void extend(MeasurementState *s, int pcrnum, uint8_t *data)
{
    uint8_t *result;
    char tmpbuf[40];
    Error *err;
    size_t resultlen = 0;

    memcpy(tmpbuf, s->measurements[pcrnum], 20);
    memcpy(tmpbuf + 20, data, 20);
    qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA1, tmpbuf, 40, &result, &resultlen, &err);
    memcpy(s->measurements[pcrnum], result, 20);
    g_free(result);
}

static void measurement_value(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MeasurementState *s = opaque;

    s->tmpmeasurement[s->write_count++] = val;
    if (s->write_count == 20) {
        extend(s, s->pcr, s->tmpmeasurement);
        s->write_count = 0;
    }
}

void extend_data(int pcrnum, uint8_t *data, size_t len)
{
    uint8_t *result;
    Error *err;
    size_t resultlen = 0;
    int ret;
    Object *obj = object_resolve_path_type("", TYPE_MEASUREMENTS, NULL);

    if (!obj) {
        return;
    }

    ret = qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA1, (char *)data, len, &result,
                             &resultlen, &err);
    if (ret < 0) {
        return;
    }

    extend(MEASUREMENT(obj), pcrnum, result);
    g_free(result);
}

static const MemoryRegionOps measurement_select_ops = {
    .write = measurement_select,
    .read = measurement_version,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps measurement_value_ops = {
    .write = measurement_value,
    .read = measurement_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void measurement_realize(DeviceState *dev, Error **errp)
{
    MeasurementState *s = MEASUREMENT(dev);

    memory_region_init_io(&s->io_select, OBJECT(s), &measurement_select_ops, s,
                          "measurement-select", 1);
    isa_register_ioport(&s->parent_obj, &s->io_select, s->iobase);
    memory_region_init_io(&s->io_value, OBJECT(s), &measurement_value_ops, s,
                          "measurement-value", 1);
    isa_register_ioport(&s->parent_obj, &s->io_value, s->iobase + 1);
    measurement_reset(dev);
}

static Property measurement_props[] = {
    DEFINE_PROP_UINT16(MEASUREMENTS_PROP_IO_BASE, MeasurementState, iobase, 0x620),
    DEFINE_PROP_END_OF_LIST(),
};

static void measurement_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    fprintf(stderr, "CLASS INIT\n");
    dc->realize = measurement_realize;
    dc->reset = measurement_reset;
    dc->props = measurement_props;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo measurement = {
    .name          = TYPE_MEASUREMENTS,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(MeasurementState),
    .class_init    = measurement_class_init,
};

static void measurement_register_types(void)
{
    type_register_static(&measurement);
}

type_init(measurement_register_types);

void print_measurements(Monitor *mon, const QDict *qdict)
{
    int i, j;
    Object *obj = object_resolve_path_type("", TYPE_MEASUREMENTS, NULL);
    MeasurementState *s;

    if (!obj) {
        return;
    }

    s = MEASUREMENT(obj);

    for (i = 0; i < 24; i++) {
        monitor_printf(mon, "0x%02x: ", i);
        for (j = 0; j < 20; j++) {
            monitor_printf(mon, "0x%02x ", s->measurements[i][j]);
        }
        monitor_printf(mon, "\n");
    }
}
