/*
 * QEMU emulated lid button device
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

#include "hw/acpi/button.h"

#define BUTTON_DEVICE(obj) OBJECT_CHECK(BUTTONState, (obj), \
                                        TYPE_BUTTON)

#define BUTTON_STA_ADDR            0

#define PROCFS_PATH                "/proc/acpi/button"
#define LID_DIR                    "lid"
#define LID_STATE_FILE             "state"
#define MIN_BUTTON_PROBE_INTERVAL  10  /* ms */
#define MAX_ALLOWED_LINE_LENGTH    32  /* For convenience when comparing */

enum {
    LID_CLOSED = 0,
    LID_OPEN = 1,
};

static const char *lid_state[] = { "closed", "open" };

typedef struct BUTTONState {
    ISADevice dev;
    MemoryRegion io;
    uint16_t ioport;
    uint8_t lid_state;
    bool use_qmp_control;
    bool qmp_lid_open;
    bool enable_procfs;

    QEMUTimer *probe_state_timer;
    uint64_t probe_state_interval;

    char *button_path;
    char lid_dir[MAX_ALLOWED_LINE_LENGTH];
} BUTTONState;

static inline bool button_file_accessible(char *path, const char *dir,
                                          char *subdir, const char *file)
{
    char full_path[PATH_MAX];
    int path_len;

    path_len = snprintf(full_path, PATH_MAX, "%s/%s/%s/%s", path, dir, subdir,
                        file);
    if (path_len < 0 || path_len >= PATH_MAX) {
        return false;
    }

    if (access(full_path, R_OK) == 0) {
        return true;
    }
    return false;
}

static void button_get_lid_state(BUTTONState *s)
{
    char file_path[PATH_MAX];
    int path_len;
    char line[MAX_ALLOWED_LINE_LENGTH];
    FILE *ff;

    path_len = snprintf(file_path, PATH_MAX, "%s/%s/%s/%s", s->button_path,
                        LID_DIR, s->lid_dir, LID_STATE_FILE);
    if (path_len < 0 || path_len >= PATH_MAX) {
        warn_report("Could not read the lid state.");
        return;
    }

    ff = fopen(file_path, "r");
    if (ff == NULL) {
        warn_report("Could not read the lid state.");
        return;
    }

    if (fgets(line, MAX_ALLOWED_LINE_LENGTH, ff) == NULL) {
        warn_report("Lid state unreadable.");
    } else {
        if (strstr(line, lid_state[LID_OPEN]) != NULL) {
            s->lid_state = LID_OPEN;
        } else if (strstr(line, lid_state[LID_CLOSED]) != NULL) {
            s->lid_state = LID_CLOSED;
        } else {
            warn_report("Lid state undetermined.");
        }
    }

    fclose(ff);
}

static void button_get_dynamic_status(BUTTONState *s)
{
    trace_button_get_dynamic_status();

    if (s->use_qmp_control) {
        s->lid_state = s->qmp_lid_open ? LID_OPEN : LID_CLOSED;
    } else if (s->enable_procfs) {
        button_get_lid_state(s);
    } else {
        s->lid_state = LID_CLOSED;
    }
}

static void button_probe_state(void *opaque)
{
    BUTTONState *s = opaque;

    uint8_t lid_state_before = s->lid_state;

    button_get_dynamic_status(s);

    if (lid_state_before != s->lid_state) {
        Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
        acpi_send_event(DEVICE(obj), ACPI_BUTTON_CHANGE_STATUS);
    }
    timer_mod(s->probe_state_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              s->probe_state_interval);
}

static void button_probe_state_timer_init(BUTTONState *s)
{
    if (s->enable_procfs && s->probe_state_interval > 0) {
        s->probe_state_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                            button_probe_state, s);
        timer_mod(s->probe_state_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                  s->probe_state_interval);
    }
}

static inline bool button_verify_lid_procfs(char *path, char *lid_subdir)
{
    return button_file_accessible(path, LID_DIR, lid_subdir, LID_STATE_FILE);
}

static bool button_get_lid_dir(BUTTONState *s, char *path)
{
    DIR *dir;
    char lid_path[PATH_MAX];
    int path_len;
    struct dirent *ent;

    path_len = snprintf(lid_path, PATH_MAX, "%s/%s", path, LID_DIR);
    if (path_len < 0 || path_len >= PATH_MAX) {
        return false;
    }

    dir = opendir(lid_path);
    if (dir == NULL) {
        return false;
    }

    ent = readdir(dir);
    while (ent != NULL) {
        if (ent->d_name[0] != '.') {
            if (button_verify_lid_procfs(path, ent->d_name)) {
                path_len = snprintf(s->lid_dir, strlen(ent->d_name) + 1, "%s",
                                    ent->d_name);
                if (path_len < 0 || path_len > strlen(ent->d_name)) {
                    return false;
                }
                closedir(dir);
                return true;
            }
        }
        ent = readdir(dir);
    }
    closedir(dir);
    return false;
}

static bool get_button_path(DeviceState *dev)
{
    BUTTONState *s = BUTTON_DEVICE(dev);
    char procfs_path[PATH_MAX];
    int path_len;

    if (s->button_path) {
        path_len = snprintf(procfs_path, strlen(s->button_path) + 1, "%s",
                            s->button_path);
        if (path_len < 0 || path_len > strlen(s->button_path)) {
            return false;
        }
    } else {
        path_len = snprintf(procfs_path, sizeof(PROCFS_PATH), "%s",
                            PROCFS_PATH);
        if (path_len < 0 || path_len >= sizeof(PROCFS_PATH)) {
            return false;
        }
    }

    if (button_get_lid_dir(s, procfs_path)) {
        qdev_prop_set_string(dev, BUTTON_PATH_PROP, procfs_path);
        return true;
    }

    return false;
}

static void button_realize(DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    BUTTONState *s = BUTTON_DEVICE(dev);
    FWCfgState *fw_cfg = fw_cfg_find();
    uint16_t *button_port;
    char err_details[32] = {};

    trace_button_realize();

    if (s->use_qmp_control && s->enable_procfs) {
        error_setg(errp, "Cannot enable both QMP control and procfs mode");
        return;
    }

    /* Initialize lid to open by default when in QMP mode */
    if (s->use_qmp_control) {
        s->qmp_lid_open = true;
    }

    if (s->probe_state_interval < MIN_BUTTON_PROBE_INTERVAL) {
        error_setg(errp, "'probe_state_interval' must be greater than %d ms",
                   MIN_BUTTON_PROBE_INTERVAL);
        return;
    }

    if (s->enable_procfs) {
        if (!s->button_path) {
            strcpy(err_details, " Try using 'procfs_path='");
        }

        if (!get_button_path(dev)) {
            error_setg(errp, "Button procfs path not found or unreadable.%s",
                       err_details);
            return;
        }
    }

    isa_register_ioport(d, &s->io, s->ioport);

    button_probe_state_timer_init(s);

    if (!fw_cfg) {
        return;
    }

    button_port = g_malloc(sizeof(*button_port));
    *button_port = cpu_to_le16(s->ioport);
    fw_cfg_add_file(fw_cfg, "etc/button-port", button_port,
                    sizeof(*button_port));
}

static const Property button_device_properties[] = {
    DEFINE_PROP_UINT16(BUTTON_IOPORT_PROP, BUTTONState, ioport, 0x53d),
    DEFINE_PROP_BOOL("use-qmp", BUTTONState, use_qmp_control, true),
    DEFINE_PROP_BOOL("enable-procfs", BUTTONState, enable_procfs, false),
    DEFINE_PROP_UINT64(BUTTON_PROBE_STATE_INTERVAL, BUTTONState,
                       probe_state_interval, 2000),
    DEFINE_PROP_STRING(BUTTON_PATH_PROP, BUTTONState, button_path),
};

static const VMStateDescription button_vmstate = {
    .name = "button",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(ioport, BUTTONState),
        VMSTATE_UINT64(probe_state_interval, BUTTONState),
        VMSTATE_END_OF_LIST()
    }
};

static void build_button_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *dev, *field, *method;
    Aml *button_state;
    Aml *sb_scope;
    BUTTONState *s = BUTTON_DEVICE(adev);

    button_state = aml_local(0);

    sb_scope = aml_scope("\\_SB");
    dev = aml_device("LID0");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0C0D")));

    aml_append(dev, aml_operation_region("LSTA", AML_SYSTEM_IO,
                                         aml_int(s->ioport),
                                         BUTTON_LEN));
    field = aml_field("LSTA", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("LIDS", 8));
    aml_append(dev, field);

    method = aml_method("_LID", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_name("LIDS"), button_state));
    aml_append(method, aml_return(button_state));
    aml_append(dev, method);

    aml_append(sb_scope, dev);
    aml_append(scope, sb_scope);

    /* Status Change */
    method = aml_method("\\_GPE._E0B", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.LID0"), aml_int(0x80)));
    aml_append(scope, method);
}

static void button_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(class);

    dc->realize = button_realize;
    device_class_set_props(dc, button_device_properties);
    dc->vmsd = &button_vmstate;
    adevc->build_dev_aml = build_button_aml;
}

static uint64_t button_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    BUTTONState *s = opaque;

    button_get_dynamic_status(s);

    switch (addr) {
    case BUTTON_STA_ADDR:
        return s->lid_state;
    default:
        warn_report("Button: guest read unknown value.");
        trace_button_ioport_read_unknown();
        return 0;
    }
}

static const MemoryRegionOps button_ops = {
    .read = button_ioport_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void button_instance_init(Object *obj)
{
    BUTTONState *s = BUTTON_DEVICE(obj);

    memory_region_init_io(&s->io, obj, &button_ops, s, "button",
                          BUTTON_LEN);
}

static const TypeInfo button_info = {
    .name          = TYPE_BUTTON,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(BUTTONState),
    .class_init    = button_class_init,
    .instance_init = button_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static BUTTONState *find_button_device(void)
{
    Object *o = object_resolve_path_type("", TYPE_BUTTON, NULL);
    if (!o) {
        return NULL;
    }
    return BUTTON_DEVICE(o);
}

void qmp_lid_button_set_state(bool open, Error **errp)
{
    BUTTONState *s = find_button_device();

    if (!s) {
        error_setg(errp, "No lid button device found");
        return;
    }

    s->qmp_lid_open = open;

    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    if (obj) {
        acpi_send_event(DEVICE(obj), ACPI_BUTTON_CHANGE_STATUS);
    }
}

LidButtonInfo *qmp_query_lid_button(Error **errp)
{
    BUTTONState *s = find_button_device();
    LidButtonInfo *ret;

    if (!s) {
        error_setg(errp, "No lid button device found");
        return NULL;
    }

    ret = g_new0(LidButtonInfo, 1);

    if (s->use_qmp_control) {
        ret->open = s->qmp_lid_open;
    } else {
        button_get_dynamic_status(s);
        ret->open = (s->lid_state == LID_OPEN);
    }

    return ret;
}

static void button_register_types(void)
{
    type_register_static(&button_info);
}

type_init(button_register_types)
