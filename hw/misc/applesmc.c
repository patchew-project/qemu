/*
 *  Apple SMC controller
 *
 *  Copyright (c) 2007 Alexander Graf
 *
 *  Authors: Alexander Graf <agraf@suse.de>
 *           Susanne Graf <suse@csgraf.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************
 *
 * In all Intel-based Apple hardware there is an SMC chip to control the
 * backlight, fans and several other generic device parameters. It also
 * contains the magic keys used to dongle Mac OS X to the device.
 *
 * This driver was mostly created by looking at the Linux AppleSMC driver
 * implementation and does not support IRQ.
 *
 */

#include "qemu/osdep.h"
#include "hw/isa/isa.h"
#include "hw/core/qdev-properties.h"
#include "ui/console.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "system/runstate.h"
#include "trace.h"

/* #define DEBUG_SMC */

#define APPLESMC_DEFAULT_IOBASE        0x300
#define TYPE_APPLE_SMC "isa-applesmc"
#define APPLESMC_MAX_DATA_LENGTH       32
#define APPLESMC_PROP_IO_BASE "iobase"

enum {
    APPLESMC_DATA_PORT               = 0x00,
    APPLESMC_CMD_PORT                = 0x04,
    APPLESMC_ERR_PORT                = 0x1e,
    APPLESMC_NUM_PORTS               = 0x20,
};

enum {
    APPLESMC_READ_CMD                = 0x10,
    APPLESMC_WRITE_CMD               = 0x11,
    APPLESMC_GET_KEY_BY_INDEX_CMD    = 0x12,
    APPLESMC_GET_KEY_TYPE_CMD        = 0x13,
};

enum {
    APPLESMC_ST_CMD_DONE             = 0x00,
    APPLESMC_ST_DATA_READY           = 0x01,
    APPLESMC_ST_BUSY                 = 0x02,
    APPLESMC_ST_ACK                  = 0x04,
    APPLESMC_ST_NEW_CMD              = 0x08,
};

enum {
    APPLESMC_ST_1E_CMD_INTRUPTED     = 0x80,
    APPLESMC_ST_1E_STILL_BAD_CMD     = 0x81,
    APPLESMC_ST_1E_BAD_CMD           = 0x82,
    APPLESMC_ST_1E_NOEXIST           = 0x84,
    APPLESMC_ST_1E_WRITEONLY         = 0x85,
    APPLESMC_ST_1E_READONLY          = 0x86,
    APPLESMC_ST_1E_BAD_INDEX         = 0xb8,
};

/*
 * Job codes written to the "NATJ" key, and implied by "OSWD": the action the
 * SMC watchdog takes when its countdown (seeded from "NATi"/"OSWD") elapses
 * because the guest failed to power down in time.
 */
enum {
    APPLESMC_WDT_DISABLE             = 0,
    APPLESMC_WDT_SHUTDOWN            = 1,
    APPLESMC_WDT_RESTART             = 2,
};

#ifdef DEBUG_SMC
#define smc_debug(...) fprintf(stderr, "AppleSMC: " __VA_ARGS__)
#else
#define smc_debug(...) do { } while (0)
#endif

static char default_osk[64] = "This is a dummy key. Enter the real key "
                              "using the -osk parameter";

struct AppleSMCData {
    uint8_t len;
    const char *key;
    const char *type;
    const char *data;
    QLIST_ENTRY(AppleSMCData) node;
};

OBJECT_DECLARE_SIMPLE_TYPE(AppleSMCState, APPLE_SMC)

struct AppleSMCState {
    ISADevice parent_obj;

    MemoryRegion io_data;
    MemoryRegion io_cmd;
    MemoryRegion io_err;
    uint32_t iobase;
    uint8_t cmd;
    uint8_t status;
    uint8_t status_1e;
    uint8_t last_ret;
    char key[4];
    uint8_t read_pos;
    uint8_t data_len;
    uint8_t data_pos;
    uint8_t data[255];
    char *osk;
    QLIST_HEAD(, AppleSMCData) data_def;

    QEMUTimer *wdt_timer;   /* shutdown watchdog, armed via NATi/NATJ/OSWD */
    uint16_t wdt_timeout;   /* countdown in seconds */
    uint8_t wdt_job;        /* action on expiry (APPLESMC_WDT_*) */
};

static void applesmc_io_cmd_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    AppleSMCState *s = opaque;
    uint8_t status = s->status & 0x0f;

    trace_applesmc_cmd_write((uint8_t)val, s->status);
    smc_debug("CMD received: 0x%02x\n", (uint8_t)val);
    switch (val) {
    case APPLESMC_READ_CMD:
    case APPLESMC_WRITE_CMD:
    case APPLESMC_GET_KEY_TYPE_CMD:
        /* did last command run through OK? */
        if (status == APPLESMC_ST_CMD_DONE || status == APPLESMC_ST_NEW_CMD) {
            s->cmd = val;
            s->status = APPLESMC_ST_NEW_CMD | APPLESMC_ST_ACK;
            trace_applesmc_cmd_accepted((uint8_t)val);
        } else {
            smc_debug("ERROR: previous command interrupted!\n");
            s->status = APPLESMC_ST_NEW_CMD;
            s->status_1e = APPLESMC_ST_1E_CMD_INTRUPTED;
        }
        break;
    default:
        smc_debug("UNEXPECTED CMD 0x%02x\n", (uint8_t)val);
        s->status = APPLESMC_ST_NEW_CMD;
        s->status_1e = APPLESMC_ST_1E_BAD_CMD;
        trace_applesmc_cmd_rejected((uint8_t)val, s->status_1e);
    }
    s->read_pos = 0;
    s->data_pos = 0;
}

static const struct AppleSMCData *applesmc_find_key(AppleSMCState *s)
{
    struct AppleSMCData *d;

    QLIST_FOREACH(d, &s->data_def, node) {
        if (!memcmp(d->key, s->key, 4)) {
            return d;
        }
    }
    return NULL;
}

static void applesmc_wdt_expired(void *opaque)
{
    AppleSMCState *s = opaque;

    trace_applesmc_wdt_expired(s->wdt_job);
    warn_report("applesmc: watchdog expired, forcing the guest down");
    if (s->wdt_job == APPLESMC_WDT_SHUTDOWN) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    } else {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void applesmc_wdt_set(AppleSMCState *s, uint8_t job, uint16_t seconds)
{
    s->wdt_job = job;
    if (job == APPLESMC_WDT_DISABLE || seconds == 0) {
        timer_del(s->wdt_timer);
        trace_applesmc_wdt_disarm();
        return;
    }
    timer_mod(s->wdt_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              (int64_t)seconds * NANOSECONDS_PER_SECOND);
    trace_applesmc_wdt_arm(seconds, job);
}

/* Act on a guest write once its full payload has been received. */
static void applesmc_write_key(AppleSMCState *s)
{
    if (!memcmp(s->key, "NATi", 4) && s->data_len >= 2) {
        /* Big-endian seconds; stored only, the "NATJ" write arms the timer. */
        s->wdt_timeout = (s->data[0] << 8) | s->data[1];
    } else if (!memcmp(s->key, "NATJ", 4) && s->data_len >= 1) {
        applesmc_wdt_set(s, s->data[0], s->wdt_timeout);
    } else if (!memcmp(s->key, "OSWD", 4) && s->data_len >= 2) {
        uint16_t seconds = (s->data[0] << 8) | s->data[1];
        uint8_t job = seconds ? APPLESMC_WDT_RESTART : APPLESMC_WDT_DISABLE;

        applesmc_wdt_set(s, job, seconds);
    }
}

static void applesmc_io_data_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{
    AppleSMCState *s = opaque;
    const struct AppleSMCData *d;

    smc_debug("DATA received: 0x%02x\n", (uint8_t)val);
    trace_applesmc_data_write(s->cmd, s->read_pos, (uint8_t)val);
    switch (s->cmd) {
    case APPLESMC_READ_CMD:
        if ((s->status & 0x0f) == APPLESMC_ST_CMD_DONE) {
            break;
        }
        if (s->read_pos < 4) {
            s->key[s->read_pos] = val;
            s->status = APPLESMC_ST_ACK;
            if (s->read_pos == 3) {
                trace_applesmc_key_selected(s->key[0], s->key[1],
                                            s->key[2], s->key[3]);
            }
        } else if (s->read_pos == 4) {
            d = applesmc_find_key(s);
            if (d != NULL) {
                memcpy(s->data, d->data, d->len);
                s->data_len = d->len;
                s->data_pos = 0;
                s->status = APPLESMC_ST_ACK | APPLESMC_ST_DATA_READY;
                s->status_1e = APPLESMC_ST_CMD_DONE;  /* clear on valid key */
            } else {
                smc_debug("READ_CMD: key '%c%c%c%c' not found!\n",
                          s->key[0], s->key[1], s->key[2], s->key[3]);
                trace_applesmc_key_not_found(s->key[0], s->key[1],
                                             s->key[2], s->key[3]);
                s->status = APPLESMC_ST_CMD_DONE;
                s->status_1e = APPLESMC_ST_1E_NOEXIST;
            }
        }
        s->read_pos++;
        break;
    case APPLESMC_WRITE_CMD:
        if (s->read_pos < 4) {
            s->key[s->read_pos] = val;
            s->status = APPLESMC_ST_ACK;
            if (s->read_pos == 3) {
                trace_applesmc_key_selected(s->key[0], s->key[1],
                                            s->key[2], s->key[3]);
            }
            s->read_pos++;
        } else if (s->read_pos == 4) {
            s->data_len = val;
            s->data_pos = 0;
            s->read_pos++;
            s->status = APPLESMC_ST_ACK;
            trace_applesmc_write_len(s->key[0], s->key[1], s->key[2],
                                     s->key[3], s->data_len);
            if (s->data_len == 0) {
                applesmc_write_key(s);
                s->status = APPLESMC_ST_CMD_DONE;
                s->status_1e = APPLESMC_ST_CMD_DONE;
            }
        } else {
            if (s->data_pos < s->data_len) {
                s->data[s->data_pos] = val;
                trace_applesmc_write_data(s->key[0], s->key[1], s->key[2],
                                          s->key[3], s->data_pos, (uint8_t)val);
                s->data_pos++;
            }
            if (s->data_pos >= s->data_len) {
                applesmc_write_key(s);
                trace_applesmc_write_complete(s->key[0], s->key[1],
                                              s->key[2], s->key[3],
                                              s->data_len);
                s->status = APPLESMC_ST_CMD_DONE;
                s->status_1e = APPLESMC_ST_CMD_DONE;
            } else {
                s->status = APPLESMC_ST_ACK;
            }
        }
        break;
    case APPLESMC_GET_KEY_TYPE_CMD:
        /* Unlike a read, the guest sends only the 4 key bytes, no length. */
        if (s->read_pos < 4) {
            s->key[s->read_pos] = val;
            s->status = APPLESMC_ST_ACK;
            if (++s->read_pos == 4) {
                trace_applesmc_key_selected(s->key[0], s->key[1],
                                            s->key[2], s->key[3]);
                d = applesmc_find_key(s);
                if (d != NULL) {
                    /* key info: 1-byte size, 4-byte type, 1-byte attributes */
                    s->data[0] = d->len;
                    memcpy(&s->data[1], d->type, 4);
                    s->data[5] = 0;
                    s->data_len = 6;
                    s->data_pos = 0;
                    s->status = APPLESMC_ST_ACK | APPLESMC_ST_DATA_READY;
                    s->status_1e = APPLESMC_ST_CMD_DONE;
                    trace_applesmc_key_type(s->key[0], s->key[1], s->key[2],
                                            s->key[3], d->type[0], d->type[1],
                                            d->type[2], d->type[3], d->len);
                } else {
                    trace_applesmc_key_not_found(s->key[0], s->key[1],
                                                 s->key[2], s->key[3]);
                    s->status = APPLESMC_ST_CMD_DONE;
                    s->status_1e = APPLESMC_ST_1E_NOEXIST;
                }
            }
        }
        break;
    default:
        s->status = APPLESMC_ST_CMD_DONE;
        s->status_1e = APPLESMC_ST_1E_STILL_BAD_CMD;
    }
}

static void applesmc_io_err_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    smc_debug("ERR_CODE received: 0x%02x, ignoring!\n", (uint8_t)val);
    /* NOTE: writing to the error port not supported! */
}

static uint64_t applesmc_io_data_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSMCState *s = opaque;

    switch (s->cmd) {
    case APPLESMC_READ_CMD:
    case APPLESMC_GET_KEY_TYPE_CMD:
        if (!(s->status & APPLESMC_ST_DATA_READY)) {
            break;
        }
        if (s->data_pos < s->data_len) {
            s->last_ret = s->data[s->data_pos];
            trace_applesmc_data_read(s->key[0], s->key[1], s->key[2],
                                     s->key[3], s->data_pos, s->last_ret);
            smc_debug("READ '%c%c%c%c'[%d] = %02x\n",
                      s->key[0], s->key[1], s->key[2], s->key[3],
                      s->data_pos, s->last_ret);
            s->data_pos++;
            if (s->data_pos == s->data_len) {
                s->status = APPLESMC_ST_CMD_DONE;
                smc_debug("READ '%c%c%c%c' Len=%d complete!\n",
                          s->key[0], s->key[1], s->key[2], s->key[3],
                          s->data_len);
            } else {
                s->status = APPLESMC_ST_ACK | APPLESMC_ST_DATA_READY;
            }
        }
        break;
    default:
        s->status = APPLESMC_ST_CMD_DONE;
        s->status_1e = APPLESMC_ST_1E_STILL_BAD_CMD;
    }
    smc_debug("DATA sent: 0x%02x\n", s->last_ret);

    return s->last_ret;
}

static uint64_t applesmc_io_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSMCState *s = opaque;

    smc_debug("CMD sent: 0x%02x\n", s->status);
    return s->status;
}

static uint64_t applesmc_io_err_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleSMCState *s = opaque;

    /* NOTE: read does not clear the 1e status */
    smc_debug("ERR_CODE sent: 0x%02x\n", s->status_1e);
    return s->status_1e;
}

static void applesmc_add_key(AppleSMCState *s, const char *key,
                             const char *type, int len, const char *data)
{
    struct AppleSMCData *def;

    def = g_new0(struct AppleSMCData, 1);
    def->key = key;
    def->type = type;
    def->len = len;
    def->data = data;

    QLIST_INSERT_HEAD(&s->data_def, def, node);
}

static void qdev_applesmc_isa_reset(DeviceState *dev)
{
    AppleSMCState *s = APPLE_SMC(dev);

    s->status = 0x00;
    s->status_1e = 0x00;
    s->last_ret = 0x00;

    if (s->wdt_timer) {
        timer_del(s->wdt_timer);
    }
    s->wdt_job = APPLESMC_WDT_DISABLE;
    s->wdt_timeout = 0;
}

static const MemoryRegionOps applesmc_data_io_ops = {
    .write = applesmc_io_data_write,
    .read = applesmc_io_data_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps applesmc_cmd_io_ops = {
    .write = applesmc_io_cmd_write,
    .read = applesmc_io_cmd_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps applesmc_err_io_ops = {
    .write = applesmc_io_err_write,
    .read = applesmc_io_err_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void applesmc_isa_realize(DeviceState *dev, Error **errp)
{
    AppleSMCState *s = APPLE_SMC(dev);

    memory_region_init_io(&s->io_data, OBJECT(s), &applesmc_data_io_ops, s,
                          "applesmc-data", 1);
    isa_register_ioport(&s->parent_obj, &s->io_data,
                        s->iobase + APPLESMC_DATA_PORT);

    memory_region_init_io(&s->io_cmd, OBJECT(s), &applesmc_cmd_io_ops, s,
                          "applesmc-cmd", 1);
    isa_register_ioport(&s->parent_obj, &s->io_cmd,
                        s->iobase + APPLESMC_CMD_PORT);

    memory_region_init_io(&s->io_err, OBJECT(s), &applesmc_err_io_ops, s,
                          "applesmc-err", 1);
    isa_register_ioport(&s->parent_obj, &s->io_err,
                        s->iobase + APPLESMC_ERR_PORT);

    if (!s->osk || (strlen(s->osk) != 64)) {
        warn_report("Using AppleSMC with invalid key");
        s->osk = default_osk;
    }

    QLIST_INIT(&s->data_def);
    applesmc_add_key(s, "REV ", "{rev", 6, "\x01\x13\x0f\x00\x00\x03");
    applesmc_add_key(s, "OSK0", "ch8*", 32, s->osk);
    applesmc_add_key(s, "OSK1", "ch8*", 32, s->osk + 32);
    applesmc_add_key(s, "NATJ", "ui8 ", 1, "\x00");
    applesmc_add_key(s, "MSSP", "ui8 ", 1, "\x00");
    applesmc_add_key(s, "MSSD", "si8 ", 1, "\x03");
    applesmc_add_key(s, "NATi", "ui16", 2, "\x00\x00");
    applesmc_add_key(s, "OSWD", "ui16", 2, "\x00\x00");

    s->wdt_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, applesmc_wdt_expired, s);
}

static void applesmc_unrealize(DeviceState *dev)
{
    AppleSMCState *s = APPLE_SMC(dev);
    struct AppleSMCData *d, *next;

    if (s->wdt_timer) {
        timer_free(s->wdt_timer);
    }

    /* Remove existing entries */
    QLIST_FOREACH_SAFE(d, &s->data_def, node, next) {
        QLIST_REMOVE(d, node);
        g_free(d);
    }
}

static const Property applesmc_isa_properties[] = {
    DEFINE_PROP_UINT32(APPLESMC_PROP_IO_BASE, AppleSMCState, iobase,
                       APPLESMC_DEFAULT_IOBASE),
    DEFINE_PROP_STRING("osk", AppleSMCState, osk),
};

static void build_applesmc_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *crs;
    AppleSMCState *s = APPLE_SMC(adev);
    uint32_t iobase = s->iobase;
    Aml *dev = aml_device("SMC");

    aml_append(dev, aml_name_decl("_HID", aml_eisaid("APP0001")));
    /* device present, functioning, decoding, not shown in UI */
    aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));
    crs = aml_resource_template();
    aml_append(crs,
        aml_io(AML_DECODE16, iobase, iobase, 0x01, APPLESMC_MAX_DATA_LENGTH)
    );
    aml_append(crs, aml_irq_no_flags(6));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

static void qdev_applesmc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    dc->realize = applesmc_isa_realize;
    dc->unrealize = applesmc_unrealize;
    device_class_set_legacy_reset(dc, qdev_applesmc_isa_reset);
    device_class_set_props(dc, applesmc_isa_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    adevc->build_dev_aml = build_applesmc_aml;
}

static const TypeInfo applesmc_isa_info = {
    .name          = TYPE_APPLE_SMC,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(AppleSMCState),
    .class_init    = qdev_applesmc_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static void applesmc_register_types(void)
{
    type_register_static(&applesmc_isa_info);
}

type_init(applesmc_register_types)
