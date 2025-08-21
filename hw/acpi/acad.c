/*
 * QEMU emulated AC adapter device.
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

#include "hw/acpi/acad.h"

#define AC_ADAPTER_DEVICE(obj) OBJECT_CHECK(ACADState, (obj), \
                                            TYPE_AC_ADAPTER)

#define AC_STA_ADDR               0

#define SYSFS_PATH                "/sys/class/power_supply"
#define AC_ADAPTER_TYPE           "Mains"
#define MAX_ALLOWED_TYPE_LENGTH   16

enum {
    AC_ADAPTER_OFFLINE = 0,
    AC_ADAPTER_ONLINE = 1,
};

typedef struct ACADState {
    ISADevice dev;
    MemoryRegion io;
    uint16_t ioport;
    uint8_t state;
    bool use_qmp_control;
    bool qmp_connected;
    bool enable_sysfs;

    QEMUTimer *probe_state_timer;
    uint64_t probe_state_interval;

    char *acad_path;
} ACADState;

static const char *online_file = "online";
static const char *type_file = "type";

static inline bool acad_file_accessible(char *path, const char *file)
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

static void acad_get_state(ACADState *s)
{
    char file_path[PATH_MAX];
    int path_len;
    uint8_t val;
    FILE *ff;

    path_len = snprintf(file_path, PATH_MAX, "%s/%s", s->acad_path,
                        online_file);
    if (path_len < 0 || path_len >= PATH_MAX) {
        warn_report("Could not read the AC adapter state.");
        return;
    }

    ff = fopen(file_path, "r");
    if (ff == NULL) {
        warn_report("Could not read the AC adapter state.");
        return;
    }

    if (!fscanf(ff, "%hhu", &val)) {
        warn_report("AC adapter state unreadable.");
    } else {
        switch (val) {
        case AC_ADAPTER_OFFLINE:
        case AC_ADAPTER_ONLINE:
            s->state = val;
            break;
        default:
            warn_report("AC adapter state undetermined.");
        }
    }
    fclose(ff);
}

static void acad_get_dynamic_status(ACADState *s)
{
    if (s->use_qmp_control) {
        s->state = s->qmp_connected ? AC_ADAPTER_ONLINE : AC_ADAPTER_OFFLINE;
    } else if (s->enable_sysfs) {
        acad_get_state(s);
    } else {
        s->state = AC_ADAPTER_OFFLINE;
    }

    trace_acad_get_dynamic_status(s->state);
}

static void acad_probe_state(void *opaque)
{
    ACADState *s = opaque;

    uint8_t state_before = s->state;

    acad_get_dynamic_status(s);

    if (state_before != s->state) {
        Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
        acpi_send_event(DEVICE(obj), ACPI_AC_ADAPTER_CHANGE_STATUS);
    }
    timer_mod(s->probe_state_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              s->probe_state_interval);
}

static void acad_probe_state_timer_init(ACADState *s)
{
    if (s->enable_sysfs && s->probe_state_interval > 0) {
        s->probe_state_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                            acad_probe_state, s);
        timer_mod(s->probe_state_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                  s->probe_state_interval);
    }
}

static bool acad_verify_sysfs(ACADState *s, char *path)
{
    FILE *ff;
    char type_path[PATH_MAX];
    int path_len;
    char val[MAX_ALLOWED_TYPE_LENGTH];

    path_len = snprintf(type_path, PATH_MAX, "%s/%s", path, type_file);
    if (path_len < 0 || path_len >= PATH_MAX) {
        return false;
    }

    ff = fopen(type_path, "r");
    if (ff == NULL) {
        return false;
    }

    if (fgets(val, MAX_ALLOWED_TYPE_LENGTH, ff) == NULL) {
        fclose(ff);
        return false;
    } else {
        val[strcspn(val, "\n")] = 0;
        if (strncmp(val, AC_ADAPTER_TYPE, MAX_ALLOWED_TYPE_LENGTH)) {
            fclose(ff);
            return false;
        }
    }
    fclose(ff);

    return acad_file_accessible(path, online_file);
}

static bool get_acad_path(DeviceState *dev)
{
    ACADState *s = AC_ADAPTER_DEVICE(dev);
    DIR *dir;
    struct dirent *ent;
    char bp[PATH_MAX];
    int path_len;

    if (s->acad_path) {
        return acad_verify_sysfs(s, s->acad_path);
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
            if (acad_verify_sysfs(s, bp)) {
                qdev_prop_set_string(dev, AC_ADAPTER_PATH_PROP, bp);
                closedir(dir);
                return true;
            }
        }
        ent = readdir(dir);
    }
    closedir(dir);

    return false;
}

static void acad_realize(DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    ACADState *s = AC_ADAPTER_DEVICE(dev);
    FWCfgState *fw_cfg = fw_cfg_find();
    uint16_t *acad_port;
    char err_details[32] = {};

    trace_acad_realize();

    if (s->use_qmp_control && s->enable_sysfs) {
        error_setg(errp, "Cannot enable both QMP control and sysfs mode");
        return;
    }

    if (s->enable_sysfs) {
        if (!s->acad_path) {
            strcpy(err_details, " Try using 'sysfs_path='");
        }

        if (!get_acad_path(dev)) {
            error_setg(errp, "AC adapter sysfs path not found or unreadable.%s",
                       err_details);
            return;
        }
    }

    isa_register_ioport(d, &s->io, s->ioport);

    acad_probe_state_timer_init(s);

    if (!fw_cfg) {
        return;
    }

    acad_port = g_malloc(sizeof(*acad_port));
    *acad_port = cpu_to_le16(s->ioport);
    fw_cfg_add_file(fw_cfg, "etc/acad-port", acad_port,
                    sizeof(*acad_port));
}

static const Property acad_device_properties[] = {
    DEFINE_PROP_UINT16(AC_ADAPTER_IOPORT_PROP, ACADState, ioport, 0x53c),
    DEFINE_PROP_BOOL("use-qmp", ACADState, use_qmp_control, true),
    DEFINE_PROP_BOOL("enable-sysfs", ACADState, enable_sysfs, false),
    DEFINE_PROP_UINT64(AC_ADAPTER_PROBE_STATE_INTERVAL, ACADState,
                       probe_state_interval, 2000),
    DEFINE_PROP_STRING(AC_ADAPTER_PATH_PROP, ACADState, acad_path),
};

static const VMStateDescription acad_vmstate = {
    .name = "acad",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(ioport, ACADState),
        VMSTATE_UINT64(probe_state_interval, ACADState),
        VMSTATE_END_OF_LIST()
    }
};

static void build_acad_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *dev, *field, *method, *pkg;
    Aml *acad_state;
    Aml *sb_scope;
    ACADState *s = AC_ADAPTER_DEVICE(adev);

    acad_state  = aml_local(0);

    sb_scope = aml_scope("\\_SB");
    dev = aml_device("ADP0");
    aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0003")));

    aml_append(dev, aml_operation_region("ACST", AML_SYSTEM_IO,
                                         aml_int(s->ioport),
                                         AC_ADAPTER_LEN));
    field = aml_field("ACST", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PWRS", 8));
    aml_append(dev, field);

    method = aml_method("_PSR", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_name("PWRS"), acad_state));
    aml_append(method, aml_return(acad_state));
    aml_append(dev, method);

    method = aml_method("_PCL", 0, AML_NOTSERIALIZED);
    pkg = aml_package(1);
    aml_append(pkg, aml_name("_SB"));
    aml_append(method, aml_return(pkg));
    aml_append(dev, method);

    method = aml_method("_PIF", 0, AML_NOTSERIALIZED);
    pkg = aml_package(6);
    /* Power Source State */
    aml_append(pkg, aml_int(0));  /* Non-redundant, non-shared */
    /* Maximum Output Power */
    aml_append(pkg, aml_int(AC_ADAPTER_VAL_UNKNOWN));
    /* Maximum Input Power */
    aml_append(pkg, aml_int(AC_ADAPTER_VAL_UNKNOWN));
    /* Model Number */
    aml_append(pkg, aml_string("QADP001"));
    /* Serial Number */
    aml_append(pkg, aml_string("SN00000"));
    /* OEM Information */
    aml_append(pkg, aml_string("QEMU"));
    aml_append(method, aml_return(pkg));
    aml_append(dev, method);

    aml_append(sb_scope, dev);
    aml_append(scope, sb_scope);

    /* Status Change */
    method = aml_method("\\_GPE._E0A", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.ADP0"), aml_int(0x80)));
    aml_append(scope, method);
}

static void acad_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(class);

    dc->realize = acad_realize;
    device_class_set_props(dc, acad_device_properties);
    dc->vmsd = &acad_vmstate;
    adevc->build_dev_aml = build_acad_aml;
}

static uint64_t acad_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    ACADState *s = opaque;

    acad_get_dynamic_status(s);

    switch (addr) {
    case AC_STA_ADDR:
        return s->state;
    default:
        warn_report("AC adapter: guest read unknown value.");
        trace_acad_ioport_read_unknown();
        return 0;
    }
}

static const MemoryRegionOps acad_ops = {
    .read = acad_ioport_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void acad_instance_init(Object *obj)
{
    ACADState *s = AC_ADAPTER_DEVICE(obj);

    memory_region_init_io(&s->io, obj, &acad_ops, s, "acad",
                          AC_ADAPTER_LEN);
}

static const TypeInfo acad_info = {
    .name          = TYPE_AC_ADAPTER,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ACADState),
    .class_init    = acad_class_init,
    .instance_init = acad_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static ACADState *find_acad_device(void)
{
    Object *o = object_resolve_path_type("", TYPE_AC_ADAPTER, NULL);
    if (!o) {
        return NULL;
    }
    return AC_ADAPTER_DEVICE(o);
}

void qmp_ac_adapter_set_state(bool connected, Error **errp)
{
    ACADState *s = find_acad_device();

    if (!s) {
        error_setg(errp, "No AC adapter device found");
        return;
    }

    s->qmp_connected = connected;

    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    if (obj) {
        acpi_send_event(DEVICE(obj), ACPI_AC_ADAPTER_CHANGE_STATUS);
    }
}

AcAdapterInfo *qmp_query_ac_adapter(Error **errp)
{
    ACADState *s = find_acad_device();
    AcAdapterInfo *ret;

    if (!s) {
        error_setg(errp, "No AC adapter device found");
        return NULL;
    }

    ret = g_new0(AcAdapterInfo, 1);

    if (s->use_qmp_control) {
        ret->connected = s->qmp_connected;
    } else {
        acad_get_dynamic_status(s);
        ret->connected = (s->state == AC_ADAPTER_ONLINE);
    }

    return ret;
}

static void acad_register_types(void)
{
    type_register_static(&acad_info);
}

type_init(acad_register_types)
