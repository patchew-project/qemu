/*
 * QEMU emulated battery device.
 *
 * Copyright (c) 2019 Janus Technologies, Inc. (http://janustech.com)
 *
 * Authors:
 *     Leonid Bloch <lb.workbox@gmail.com>
 *     Marcel Apfelbaum <marcel.apfelbaum@gmail.com>
 *     Dmitry Fleytman <dmitry.fleytman@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory for details.
 *
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "hw/isa/isa.h"
#include "hw/acpi/acpi.h"
#include "hw/nvram/fw_cfg.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#include "hw/acpi/battery.h"

#define BATTERY_DEVICE(obj) OBJECT_CHECK(BatteryState, (obj), TYPE_BATTERY)

#define BATTERY_DISCHARGING  1
#define BATTERY_CHARGING     2

#define SYSFS_PATH       "/sys/class/power_supply"
#define BATTERY_TYPE     "Battery"

#define MAX_ALLOWED_STATE_LENGTH  32  /* For convinience when comparing */

#define NORMALIZE_BY_FULL(val, full) \
    ((full == 0) ? BATTERY_VAL_UNKNOWN \
     : (uint32_t)(val * BATTERY_FULL_CAP / full))

typedef union bat_metric {
    uint32_t val;
    uint8_t acc[4];
} bat_metric;

typedef struct BatteryState {
    ISADevice dev;
    MemoryRegion io;
    uint16_t ioport;
    bat_metric state;
    bat_metric rate;
    bat_metric charge;
    uint32_t charge_full;
    int units;  /* 0 - mWh, 1 - mAh */

    QEMUTimer *probe_state_timer;
    uint64_t probe_state_interval;

    char *bat_path;
} BatteryState;

/* Access addresses */
enum acc_addr {
    bsta_addr0, bsta_addr1, bsta_addr2, bsta_addr3,
    brte_addr0, brte_addr1, brte_addr2, brte_addr3,
    bcrg_addr0, bcrg_addr1, bcrg_addr2, bcrg_addr3
};

/* Files used when the units are:      mWh             mAh      */
static const char *full_file[] = { "energy_full", "charge_full" };
static const char *now_file[]  = { "energy_now",  "charge_now"  };
static const char *rate_file[] = { "power_now",   "current_now" };

static const char *stat_file = "status";
static const char *type_file = "type";

static const char *discharging_states[] = { "Discharging", "Not charging" };
static const char *charging_states[] = { "Charging", "Full", "Unknown" };

static inline bool battery_file_accessible(char *path, const char *file)
{
    char full_path[PATH_MAX];
    int path_len;

    path_len = snprintf(full_path, PATH_MAX, "%s/%s", path, file);
    if (path_len < 0 || path_len >= PATH_MAX) {
        return false;
    }
    if (access(full_path, R_OK) == 0) {
        return true;
    }
    return false;
}

static inline int battery_select_file(char *path, const char **file)
{
    if (battery_file_accessible(path, file[0])) {
        return 0;
    } else if (battery_file_accessible(path, file[1])) {
        return 1;
    } else {
        return -1;
    }
}

static void battery_get_full_charge(BatteryState *s, Error **errp)
{
    char file_path[PATH_MAX];
    int path_len;
    uint32_t val;
    FILE *ff;

    path_len = snprintf(file_path, PATH_MAX, "%s/%s", s->bat_path,
                        full_file[s->units]);
    if (path_len < 0 || path_len >= PATH_MAX) {
        error_setg(errp, "Full capacity file path is inaccessible.");
        return;
    }

    ff = fopen(file_path, "r");
    if (ff == NULL) {
        error_setg_errno(errp, errno, "Could not read the full charge file.");
        return;
    }

    if (fscanf(ff, "%u", &val) != 1) {
        error_setg(errp, "Full capacity undetermined.");
        return;
    } else {
        s->charge_full = val;
    }
    fclose(ff);
}

static inline bool battery_is_discharging(char *val)
{
    static const int discharging_len = ARRAY_SIZE(discharging_states);
    int i;

    for (i = 0; i < discharging_len; i++) {
        if (!strncmp(val, discharging_states[i], MAX_ALLOWED_STATE_LENGTH)) {
            return true;
        }
    }
    return false;
}

static inline bool battery_is_charging(char *val)
{
    static const int charging_len = ARRAY_SIZE(charging_states);
    int i;

    for (i = 0; i < charging_len; i++) {
        if (!strncmp(val, charging_states[i], MAX_ALLOWED_STATE_LENGTH)) {
            return true;
        }
    }
    return false;
}

static void battery_get_state(BatteryState *s)
{
    char file_path[PATH_MAX];
    int path_len;
    char val[MAX_ALLOWED_STATE_LENGTH];
    FILE *ff;

    path_len = snprintf(file_path, PATH_MAX, "%s/%s", s->bat_path, stat_file);
    if (path_len < 0 || path_len >= PATH_MAX) {
        warn_report("Could not read the battery state.");
        return;
    }

    ff = fopen(file_path, "r");
    if (ff == NULL) {
        warn_report("Could not read the battery state.");
        return;
    }

    if (fgets(val, MAX_ALLOWED_STATE_LENGTH, ff) == NULL) {
        warn_report("Battery state unreadable.");
    } else {
        val[strcspn(val, "\n")] = 0;
        if (battery_is_discharging(val)) {
            s->state.val = BATTERY_DISCHARGING;
        } else if (battery_is_charging(val)) {
            s->state.val = BATTERY_CHARGING;
        } else {
            warn_report("Battery state undetermined.");
        }
    }
    fclose(ff);
}

static void battery_get_rate(BatteryState *s)
{
    char file_path[PATH_MAX];
    int path_len;
    uint64_t val;
    FILE *ff;

    path_len = snprintf(file_path, PATH_MAX, "%s/%s", s->bat_path,
                        rate_file[s->units]);
    if (path_len < 0 || path_len >= PATH_MAX) {
        warn_report("Could not read the battery rate.");
        s->rate.val = BATTERY_VAL_UNKNOWN;
        return;
    }

    ff = fopen(file_path, "r");
    if (ff == NULL) {
        warn_report("Could not read the battery rate.");
        s->rate.val = BATTERY_VAL_UNKNOWN;
        return;
    }

    if (fscanf(ff, "%lu", &val) != 1) {
        warn_report("Battery rate undetermined.");
        s->rate.val = BATTERY_VAL_UNKNOWN;
    } else {
        s->rate.val = NORMALIZE_BY_FULL(val, s->charge_full);
    }
    fclose(ff);
}

static void battery_get_charge(BatteryState *s)
{
    char file_path[PATH_MAX];
    int path_len;
    uint64_t val;
    FILE *ff;

    path_len = snprintf(file_path, PATH_MAX, "%s/%s", s->bat_path,
                        now_file[s->units]);
    if (path_len < 0 || path_len >= PATH_MAX) {
        warn_report("Could not read the battery charge.");
        s->charge.val = BATTERY_VAL_UNKNOWN;
        return;
    }

    ff = fopen(file_path, "r");
    if (ff == NULL) {
        warn_report("Could not read the battery charge.");
        s->charge.val = BATTERY_VAL_UNKNOWN;
        return;
    }

    if (fscanf(ff, "%lu", &val) != 1) {
        warn_report("Battery charge undetermined.");
        s->charge.val = BATTERY_VAL_UNKNOWN;
    } else {
        s->charge.val = NORMALIZE_BY_FULL(val, s->charge_full);
    }
    fclose(ff);
}

static void battery_get_dynamic_status(BatteryState *s)
{
    battery_get_state(s);
    battery_get_rate(s);
    battery_get_charge(s);

    trace_battery_get_dynamic_status(s->state.val, s->rate.val, s->charge.val);
}

static void battery_probe_state(void *opaque)
{
    BatteryState *s = opaque;

    uint32_t state_before = s->state.val;
    uint32_t rate_before = s->rate.val;
    uint32_t charge_before = s->charge.val;

    battery_get_dynamic_status(s);

    if (state_before != s->state.val || rate_before != s->rate.val ||
        charge_before != s->charge.val) {
        Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
        switch (charge_before) {
        case 0:
            break;  /* Avoid marking initiation as an update */
        default:
            acpi_send_event(DEVICE(obj), ACPI_BATTERY_CHANGE_STATUS);
        }
    }
    timer_mod(s->probe_state_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              s->probe_state_interval);
}

static void battery_probe_state_timer_init(BatteryState *s)
{
    if (s->probe_state_interval > 0) {
        s->probe_state_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                            battery_probe_state, s);
        timer_mod(s->probe_state_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                  s->probe_state_interval);
    }
}

static bool battery_verify_sysfs(BatteryState *s, char *path)
{
    int units;
    FILE *ff;
    char type_path[PATH_MAX];
    int path_len;
    char val[MAX_ALLOWED_STATE_LENGTH];

    path_len = snprintf(type_path, PATH_MAX, "%s/%s", path, type_file);
    if (path_len < 0 || path_len >= PATH_MAX) {
        return false;
    }
    ff = fopen(type_path, "r");
    if (ff == NULL) {
        return false;
    }

    if (fgets(val, MAX_ALLOWED_STATE_LENGTH, ff) == NULL) {
        fclose(ff);
        return false;
    } else {
        val[strcspn(val, "\n")] = 0;
        if (strncmp(val, BATTERY_TYPE, MAX_ALLOWED_STATE_LENGTH)) {
            fclose(ff);
            return false;
        }
    }
    fclose(ff);

    units = battery_select_file(path, full_file);

    if (units < 0) {
        return false;
    } else {
        s->units = units;
    }

    return (battery_file_accessible(path, now_file[s->units])
            & battery_file_accessible(path, rate_file[s->units])
            & battery_file_accessible(path, stat_file));
}

static bool get_battery_path(DeviceState *dev)
{
    BatteryState *s = BATTERY_DEVICE(dev);
    DIR *dir;
    struct dirent *ent;
    char bp[PATH_MAX];
    int path_len;

    if (s->bat_path) {
        return battery_verify_sysfs(s, s->bat_path);
    }

    dir = opendir(SYSFS_PATH);
    if (dir == NULL) {
        return false;
    }

    ent = readdir(dir);
    while (ent != NULL) {
        if (ent->d_name[0] != '.') {
            path_len = snprintf(bp, PATH_MAX, "%s/%s", SYSFS_PATH,
                                ent->d_name);
            if (path_len < 0 || path_len >= PATH_MAX) {
                return false;
            }
            if (battery_verify_sysfs(s, bp)) {
                qdev_prop_set_string(dev, BATTERY_PATH_PROP, bp);
                closedir(dir);
                return true;
            }
        }
        ent = readdir(dir);
    }
    closedir(dir);

    return false;
}

static void battery_realize(DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    BatteryState *s = BATTERY_DEVICE(dev);
    FWCfgState *fw_cfg = fw_cfg_find();
    uint16_t *battery_port;
    char err_details[0x20] = {};

    trace_battery_realize();

    if (!s->bat_path) {
        strcpy(err_details, " Try using 'sysfs_path='");
    }

    if (!get_battery_path(dev)) {
        error_setg(errp, "Battery sysfs path not found or unreadable.%s",
                   err_details);
        return;
    }

    battery_get_full_charge(s, errp);

    isa_register_ioport(d, &s->io, s->ioport);

    battery_probe_state_timer_init(s);

    if (!fw_cfg) {
        return;
    }

    battery_port = g_malloc(sizeof(*battery_port));
    *battery_port = cpu_to_le16(s->ioport);
    fw_cfg_add_file(fw_cfg, "etc/battery-port", battery_port,
                    sizeof(*battery_port));
}

static Property battery_device_properties[] = {
    DEFINE_PROP_UINT16(BATTERY_IOPORT_PROP, BatteryState, ioport, 0x530),
    DEFINE_PROP_UINT64(BATTERY_PROBE_STATE_INTERVAL, BatteryState,
                       probe_state_interval, 2000),
    DEFINE_PROP_STRING(BATTERY_PATH_PROP, BatteryState, bat_path),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription battery_vmstate = {
    .name = "battery",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(ioport, BatteryState),
        VMSTATE_UINT64(probe_state_interval, BatteryState),
        VMSTATE_END_OF_LIST()
    }
};

static void battery_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);

    dc->realize = battery_realize;
    device_class_set_props(dc, battery_device_properties);
    dc->vmsd = &battery_vmstate;
}

static uint64_t battery_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    BatteryState *s = opaque;

    battery_get_dynamic_status(s);

    switch (addr) {
    case bsta_addr0:
        return s->state.acc[0];
    case bsta_addr1:
        return s->state.acc[1];
    case bsta_addr2:
        return s->state.acc[2];
    case bsta_addr3:
        return s->state.acc[3];
    case brte_addr0:
        return s->rate.acc[0];
    case brte_addr1:
        return s->rate.acc[1];
    case brte_addr2:
        return s->rate.acc[2];
    case brte_addr3:
        return s->rate.acc[3];
    case bcrg_addr0:
        return s->charge.acc[0];
    case bcrg_addr1:
        return s->charge.acc[1];
    case bcrg_addr2:
        return s->charge.acc[2];
    case bcrg_addr3:
        return s->charge.acc[3];
    default:
        warn_report("Battery: guest read unknown value.");
        trace_battery_ioport_read_unknown();
        return 0;
    }
}

static const MemoryRegionOps battery_ops = {
    .read = battery_ioport_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void battery_instance_init(Object *obj)
{
    BatteryState *s = BATTERY_DEVICE(obj);

    memory_region_init_io(&s->io, obj, &battery_ops, s, "battery",
                          BATTERY_LEN);
}

static const TypeInfo battery_info = {
    .name          = TYPE_BATTERY,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(BatteryState),
    .class_init    = battery_class_init,
    .instance_init = battery_instance_init,
};

static void battery_register_types(void)
{
    type_register_static(&battery_info);
}

type_init(battery_register_types)
