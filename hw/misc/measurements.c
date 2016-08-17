/*
 * QEMU boot measurement
 *
 * Copyright (c) 2016 CoreOS, Inc <mjg59@coreos.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "crypto/hash.h"
#include "monitor/monitor.h"
#include "hw/loader.h"
#include "hw/acpi/tpm.h"
#include "hw/isa/isa.h"
#include "hw/misc/measurements.h"
#include "qmp-commands.h"
#include "sysemu/tpm.h"

#define EV_POST_CODE 1 /* Code measured during POST */

#define MEASUREMENT(obj) OBJECT_CHECK(MeasurementState, (obj), \
                                      TYPE_MEASUREMENTS)

#define MEASUREMENT_DIGEST_SIZE 20
#define MEASUREMENT_PCR_COUNT 24

typedef struct MeasurementState MeasurementState;

struct MeasurementState {
    ISADevice parent_obj;
    MemoryRegion io_select;
    MemoryRegion io_value;
    uint16_t iobase;
    uint8_t measurements[MEASUREMENT_PCR_COUNT][MEASUREMENT_DIGEST_SIZE];
    uint8_t tmpmeasurement[MEASUREMENT_DIGEST_SIZE];
    uint32_t write_count;
    uint32_t read_count;
    uint8_t pcr;
    uint32_t logsize;
    struct tpm_event *log;
};

struct tpm_event {
    uint32_t pcrindex;
    uint32_t eventtype;
    uint8_t digest[MEASUREMENT_DIGEST_SIZE];
    uint32_t eventdatasize;
    uint8_t event[0];
};

static Object *measurement_dev_find(void)
{
    return object_resolve_path_type("", TYPE_MEASUREMENTS, NULL);
}

static void measurement_reset(DeviceState *dev)
{
    MeasurementState *s = MEASUREMENT(dev);

    s->read_count = 0;
    s->write_count = 0;
    s->logsize = 0;
    memset(s->measurements, 0, sizeof(s->measurements));
    measure_roms();
}

static void measurement_select(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    MeasurementState *s = MEASUREMENT(opaque);

    if (val >= MEASUREMENT_PCR_COUNT) {
        return;
    }

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

    if (s->read_count == MEASUREMENT_DIGEST_SIZE) {
        s->read_count = 0;
    }
    return s->measurements[s->pcr][s->read_count++];
}

static void extend(MeasurementState *s, uint8_t pcrnum, uint8_t *data)
{
    Error *err;
    uint8_t tmpbuf[2 * MEASUREMENT_DIGEST_SIZE];
    size_t resultlen = 0;
    uint8_t *result = NULL;

    memcpy(tmpbuf, s->measurements[pcrnum], MEASUREMENT_DIGEST_SIZE);
    memcpy(tmpbuf + MEASUREMENT_DIGEST_SIZE, data, MEASUREMENT_DIGEST_SIZE);
    if (qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA1, (const char *)tmpbuf,
                           2 * MEASUREMENT_DIGEST_SIZE, &result, &resultlen,
                           &err) == 0) {
        memcpy(s->measurements[pcrnum], result, MEASUREMENT_DIGEST_SIZE);
    } else {
        error_reportf_err(err, "Failed to measure data:");
    }

    g_free(result);
}

static void measurement_value(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    MeasurementState *s = opaque;

    s->tmpmeasurement[s->write_count++] = val;
    if (s->write_count == MEASUREMENT_DIGEST_SIZE) {
        extend(s, s->pcr, s->tmpmeasurement);
        s->write_count = 0;
    }
}

static void log_data(MeasurementState *s, uint8_t pcrnum, uint8_t *hash,
                     char *description)
{
    int eventlen = strlen(description);
    int entrylen = eventlen + sizeof(struct tpm_event);
    struct tpm_event *logentry;

    if (!s->log) {
        return;
    }

    if (s->logsize + entrylen > TPM_LOG_AREA_MINIMUM_SIZE) {
        fprintf(stderr, "Measurement log entry would overflow log - dropping");
        return;
    }
    logentry = (struct tpm_event *)(((void *)s->log) + s->logsize);
    logentry->pcrindex = pcrnum;
    logentry->eventtype = EV_POST_CODE;
    memcpy(logentry->digest, hash, MEASUREMENT_DIGEST_SIZE);
    logentry->eventdatasize = eventlen;
    memcpy(logentry->event, description, eventlen);

    s->logsize += entrylen;
}

void measurements_extend_data(uint8_t pcrnum, uint8_t *data, size_t len,
                              char *description)
{
    int ret;
    Error *err;
    uint8_t *result;
    size_t resultlen = 0;
    Object *obj = object_resolve_path_type("", TYPE_MEASUREMENTS, NULL);

    if (!obj) {
        return;
    }

    ret = qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA1, (char *)data, len, &result,
                             &resultlen, &err);
    if (ret < 0) {
        error_reportf_err(err, "Failed to hash extension data");
    }

    extend(MEASUREMENT(obj), pcrnum, result);
    log_data(MEASUREMENT(obj), pcrnum, result, description);
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

    if (tpm_get_version() != TPM_VERSION_UNSPEC) {
        error_setg(errp, "Can't use measurements and TPM simultaneously");
        return;
    }
    memory_region_init_io(&s->io_select, OBJECT(s), &measurement_select_ops,
                          s, "measurement-select", 1);
    isa_register_ioport(&s->parent_obj, &s->io_select, s->iobase);
    memory_region_init_io(&s->io_value, OBJECT(s), &measurement_value_ops, s,
                          "measurement-value", 1);
    isa_register_ioport(&s->parent_obj, &s->io_value, s->iobase + 1);
    measurement_reset(dev);
}

static Property measurement_props[] = {
    DEFINE_PROP_UINT16(MEASUREMENTS_PROP_IO_BASE, MeasurementState, iobase,
                       0x620),
    DEFINE_PROP_END_OF_LIST(),
};

static int measurement_state_post_load(void *opaque, int version_id)
{
    MeasurementState *s = opaque;

    if (s->write_count > MEASUREMENT_DIGEST_SIZE ||
        s->read_count > MEASUREMENT_DIGEST_SIZE ||
        s->pcr > MEASUREMENT_PCR_COUNT) {
        fprintf(stderr, "Invalid measurement state on reload\n");
        return -EINVAL;
    }

    return 0;
}

static const VMStateDescription measurement_state = {
    .name = "measurements",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = measurement_state_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_2DARRAY(measurements, MeasurementState,
                              MEASUREMENT_PCR_COUNT, MEASUREMENT_DIGEST_SIZE),
        VMSTATE_BUFFER(tmpmeasurement, MeasurementState),
        VMSTATE_UINT32(write_count, MeasurementState),
        VMSTATE_UINT32(read_count, MeasurementState),
        VMSTATE_UINT8(pcr, MeasurementState),
        VMSTATE_END_OF_LIST()
    }
};

static void measurement_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = measurement_realize;
    dc->reset = measurement_reset;
    dc->props = measurement_props;
    dc->vmsd = &measurement_state;
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

MeasurementList *qmp_query_measurements(Error **errp)
{
    MeasurementList *head = NULL;
    MeasurementList **prev = &head;
    MeasurementList *elem;
    Measurement *info;
    Object *obj = object_resolve_path_type("", TYPE_MEASUREMENTS, NULL);
    MeasurementState *s;
    int pcr, i;

    if (!obj) {
        error_setg(errp, "Unable to locate measurement object");
        return NULL;
    }

    s = MEASUREMENT(obj);

    for (pcr = 0; pcr < MEASUREMENT_PCR_COUNT; pcr++) {
        info = g_new0(Measurement, 1);
        info->pcr = pcr;
        info->hash = g_malloc0(MEASUREMENT_DIGEST_SIZE * 2 + 1);
        for (i = 0; i < MEASUREMENT_DIGEST_SIZE; i++) {
            sprintf(info->hash + i * 2, "%02x", s->measurements[pcr][i]);
        }
        elem = g_new0(MeasurementList, 1);
        elem->value = info;
        *prev = elem;
        prev = &elem->next;
    }
    return head;
}

void measurements_set_log(gchar *log)
{
    Object *obj = measurement_dev_find();
    MeasurementState *s = MEASUREMENT(obj);

    s->log = (struct tpm_event *)log;
}
