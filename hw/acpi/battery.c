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
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include "hw/acpi/acpi_aml_interface.h"
#include "qapi/qapi-commands-acpi.h"

#include "hw/acpi/battery.h"

#define BATTERY_DEVICE(obj) OBJECT_CHECK(BatteryState, (obj), TYPE_BATTERY)

#define BATTERY_DISCHARGING  0x01  /* ACPI _BST bit 0 */
#define BATTERY_CHARGING     0x02  /* ACPI _BST bit 1 */
#define BATTERY_CRITICAL     0x04  /* ACPI _BST bit 2 */

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
    bool use_qmp_control;
    bool qmp_present;
    bool qmp_charging;
    bool qmp_discharging;
    int qmp_charge_percent;
    int qmp_rate;
    bool enable_sysfs;

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
            s->state.val = 0;
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
    if (s->use_qmp_control) {
        s->state.val = 0;
        if (s->qmp_present) {
            if (s->qmp_charging) {
                s->state.val |= BATTERY_CHARGING;
            }
            if (s->qmp_discharging) {
                s->state.val |= BATTERY_DISCHARGING;
            }
        }
        s->rate.val = s->qmp_rate;
        s->charge.val = (s->qmp_charge_percent * BATTERY_FULL_CAP) / 100;
    } else if (s->enable_sysfs) {
        battery_get_state(s);
        battery_get_rate(s);
        battery_get_charge(s);
    } else {
        s->state.val = 0;
        s->rate.val = 0;
        s->charge.val = 0;
    }

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
    if (s->enable_sysfs && s->probe_state_interval > 0) {
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

    if (s->use_qmp_control && s->enable_sysfs) {
        error_setg(errp, "Cannot enable both QMP control and sysfs mode");
        return;
    }

    /* Initialize QMP state to sensible defaults when in QMP mode */
    if (s->use_qmp_control) {
        s->qmp_present = true;
        s->qmp_charging = false;
        s->qmp_discharging = true;
        s->qmp_charge_percent = 50;
        s->qmp_rate = 1000;  /* 1000 mW discharge rate */
    }

    if (s->enable_sysfs) {
        if (!s->bat_path) {
            strcpy(err_details, " Try using 'sysfs_path='");
        }

        if (!get_battery_path(dev)) {
            error_setg(errp, "Battery sysfs path not found or unreadable.%s",
                       err_details);
            return;
        }
        battery_get_full_charge(s, errp);
    } else {
        s->charge_full = BATTERY_FULL_CAP;
    }

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

static const Property battery_device_properties[] = {
    DEFINE_PROP_UINT16(BATTERY_IOPORT_PROP, BatteryState, ioport, 0x530),
    DEFINE_PROP_BOOL("use-qmp", BatteryState, use_qmp_control, true),
    DEFINE_PROP_BOOL("enable-sysfs", BatteryState, enable_sysfs, false),
    DEFINE_PROP_UINT64(BATTERY_PROBE_STATE_INTERVAL, BatteryState,
                       probe_state_interval, 2000),
    DEFINE_PROP_STRING(BATTERY_PATH_PROP, BatteryState, bat_path),
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

static void build_battery_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *dev, *field, *method, *pkg;
    Aml *bat_state, *bat_rate, *bat_charge;
    Aml *sb_scope;
    BatteryState *s = BATTERY_DEVICE(adev);

    bat_state  = aml_local(0);
    bat_rate   = aml_local(1);
    bat_charge = aml_local(2);

    sb_scope = aml_scope("\\_SB");
    dev = aml_device("BAT0");
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0C0A")));

    method = aml_method("_STA", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_int(0x1F)));
    aml_append(dev, method);

    aml_append(dev, aml_operation_region("DBST", AML_SYSTEM_IO,
                                         aml_int(s->ioport),
                                         BATTERY_LEN));
    field = aml_field("DBST", AML_DWORD_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("BSTA", 32));
    aml_append(field, aml_named_field("BRTE", 32));
    aml_append(field, aml_named_field("BCRG", 32));
    aml_append(dev, field);

    method = aml_method("_BIF", 0, AML_NOTSERIALIZED);
    pkg = aml_package(13);
    /* Power Unit */
    aml_append(pkg, aml_int(0));             /* mW */
    /* Design Capacity */
    aml_append(pkg, aml_int(BATTERY_FULL_CAP));
    /* Last Full Charge Capacity */
    aml_append(pkg, aml_int(BATTERY_FULL_CAP));
    /* Battery Technology */
    aml_append(pkg, aml_int(1));             /* Secondary */
    /* Design Voltage */
    aml_append(pkg, aml_int(BATTERY_VAL_UNKNOWN));
    /* Design Capacity of Warning */
    aml_append(pkg, aml_int(BATTERY_CAPACITY_OF_WARNING));
    /* Design Capacity of Low */
    aml_append(pkg, aml_int(BATTERY_CAPACITY_OF_LOW));
    /* Battery Capacity Granularity 1 */
    aml_append(pkg, aml_int(BATTERY_CAPACITY_GRANULARITY));
    /* Battery Capacity Granularity 2 */
    aml_append(pkg, aml_int(BATTERY_CAPACITY_GRANULARITY));
    /* Model Number */
    aml_append(pkg, aml_string("QBAT001"));  /* Model Number */
    /* Serial Number */
    aml_append(pkg, aml_string("SN00000"));  /* Serial Number */
    /* Battery Type */
    aml_append(pkg, aml_string("Virtual"));  /* Battery Type */
    /* OEM Information */
    aml_append(pkg, aml_string("QEMU"));     /* OEM Information */
    aml_append(method, aml_return(pkg));
    aml_append(dev, method);

    pkg = aml_package(4);
    /* Battery State */
    aml_append(pkg, aml_int(0));
    /* Battery Present Rate */
    aml_append(pkg, aml_int(BATTERY_VAL_UNKNOWN));
    /* Battery Remaining Capacity */
    aml_append(pkg, aml_int(BATTERY_VAL_UNKNOWN));
    /* Battery Present Voltage */
    aml_append(pkg, aml_int(BATTERY_VAL_UNKNOWN));
    aml_append(dev, aml_name_decl("DBPR", pkg));

    method = aml_method("_BST", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_name("BSTA"), bat_state));
    aml_append(method, aml_store(aml_name("BRTE"), bat_rate));
    aml_append(method, aml_store(aml_name("BCRG"), bat_charge));
    aml_append(method, aml_store(bat_state,
                                 aml_index(aml_name("DBPR"), aml_int(0))));
    aml_append(method, aml_store(bat_rate,
                                 aml_index(aml_name("DBPR"), aml_int(1))));
    aml_append(method, aml_store(bat_charge,
                                 aml_index(aml_name("DBPR"), aml_int(2))));
    aml_append(method, aml_return(aml_name("DBPR")));
    aml_append(dev, method);

    aml_append(sb_scope, dev);
    aml_append(scope, sb_scope);

    /* Device Check */
    method = aml_method("\\_GPE._E07", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.BAT0"), aml_int(0x01)));
    aml_append(scope, method);

    /* Status Change */
    method = aml_method("\\_GPE._E08", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.BAT0"), aml_int(0x80)));
    aml_append(scope, method);

    /* Information Change */
    method = aml_method("\\_GPE._E09", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.BAT0"), aml_int(0x81)));
    aml_append(scope, method);
}

static void battery_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(class);

    dc->realize = battery_realize;
    device_class_set_props(dc, battery_device_properties);
    dc->vmsd = &battery_vmstate;
    adevc->build_dev_aml = build_battery_aml;
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
    .interfaces = (InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static BatteryState *find_battery_device(void)
{
    Object *o = object_resolve_path_type("", TYPE_BATTERY, NULL);
    if (!o) {
        return NULL;
    }
    return BATTERY_DEVICE(o);
}

void qmp_battery_set_state(BatteryInfo *state, Error **errp)
{
    BatteryState *s = find_battery_device();

    if (!s) {
        error_setg(errp, "No battery device found");
        return;
    }

    s->qmp_present = state->present;
    s->qmp_charging = state->charging;
    s->qmp_discharging = state->discharging;
    s->qmp_charge_percent = state->charge_percent;

    if (state->has_rate) {
        s->qmp_rate = state->rate;
    }

    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    if (obj) {
        acpi_send_event(DEVICE(obj), ACPI_BATTERY_CHANGE_STATUS);
    }
}

BatteryInfo *qmp_query_battery(Error **errp)
{
    BatteryState *s = find_battery_device();
    BatteryInfo *ret;

    if (!s) {
        error_setg(errp, "No battery device found");
        return NULL;
    }

    ret = g_new0(BatteryInfo, 1);

    if (s->use_qmp_control) {
        ret->present = s->qmp_present;
        ret->charging = s->qmp_charging;
        ret->discharging = s->qmp_discharging;
        ret->charge_percent = s->qmp_charge_percent;
        ret->has_rate = true;
        ret->rate = s->qmp_rate;
    } else {
        battery_get_dynamic_status(s);
        ret->present = true;
        ret->charging = !!(s->state.val & BATTERY_CHARGING);
        ret->discharging = !!(s->state.val & BATTERY_DISCHARGING);
        ret->charge_percent = (s->charge.val * 100) / BATTERY_FULL_CAP;
        ret->has_rate = true;
        ret->rate = s->rate.val;
    }

    ret->has_remaining_capacity = false;
    ret->has_design_capacity = true;
    ret->design_capacity = BATTERY_FULL_CAP;

    return ret;
}

static void battery_register_types(void)
{
    type_register_static(&battery_info);
}

type_init(battery_register_types)
