/*
 * Guest driven VM launch state update device via IGVM.
 * For details and specification, please look at docs/specs/vmlaunchupdate.rst.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Authors: Ani Sinha <anisinha@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef VMLAUNCHUPDATE_H
#define VMLAUNCHUPDATE_H

#include "hw/core/qdev.h"
#include "qom/object.h"
#include "qemu/units.h"
#include "system/igvm-cfg.h"

#define TYPE_VMLAUNCHUPDATE "vm-launch-update"

/* fw-cfg file definition */
#define FILE_VMLAUNCHUPDATE "etc/vmlaunchupdate"

/* version */
#define VM_LAUNCHUPDATE_VERSION 0x01

/* format bits, used by both 'capabilities' and 'control'  */

/* igvm */
#define VM_LAUNCHUPDATE_FORMAT_IGVM           (1ULL << 32)

/* 'control' field bits  */

/* disable vmlaunchupdate interface */
#define VM_LAUNCHUPDATE_CTL_DISABLE            (1 << 0)

typedef struct {
    /* api version */
    uint16_t version;

    /* VMM capabilities, read-only. */
    uint64_t capabilities;
    /* control bits, see VMFWUPDATE_CTL_* */
    uint64_t control;

    /*
     * address and size of the IGVM image.  Will be cleared when
     * the write completes successfully and IGVM file is correctly parsed.
     */
    uint64_t fw_image_addr;
    uint32_t fw_image_size;

    /*
     * address + size of opaque blob.  The guest can use this to pass on
     * information, for example which memory region the linux kernel has been
     * loaded to.  writable, will be kept intact on firmware update.
     */
    uint64_t opaque_addr;
    uint64_t opaque_size;

} VMLaunchUpdate;

typedef struct VMLaunchUpdateState {
    DeviceState parent_obj;
    VMLaunchUpdate launch_update;
    ResettableState reset_state;
} VMLaunchUpdateState;


typedef struct VMLaunchUpdateStateClass {
    ObjectClass parent_class;
} VMLaunchUpdateStateClass;

OBJECT_DECLARE_SIMPLE_TYPE(VMLaunchUpdateState, VMLAUNCHUPDATE);

/* returns NULL unless there is exactly one device */
static inline VMLaunchUpdateState *vm_launchupdate_find(void)
{
    Object *o = object_resolve_path_type("", TYPE_VMLAUNCHUPDATE, NULL);

    return o ? VMLAUNCHUPDATE(o) : NULL;
}

#endif
