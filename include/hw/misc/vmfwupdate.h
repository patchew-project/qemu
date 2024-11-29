/*
 * Guest driven VM boot component update device
 * For details and specification, please look at docs/specs/vmfwupdate.rst.
 *
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * Authors: Ani Sinha <anisinha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef VMFWUPDATE_H
#define VMFWUPDATE_H

#include "hw/qdev-core.h"
#include "qom/object.h"
#include "qemu/units.h"

#define TYPE_VMFWUPDATE "vmfwupdate"

#define VMFWUPDCAPMSK  0xffff /* least significant 16 capability bits */

#define VMFWUPDATE_CAP_EDKROM 0x08 /* bit 4 represents support for EDKROM */
#define VMFWUPDATE_CAP_BIOS_RESIZE 0x04 /* guests may resize bios region */
#define CAP_VMFWUPD_MASK 0x80

#define VMFWUPDATE_OPAQUE_SIZE (1024 * MiB)

/* fw_cfg file definitions */
#define FILE_VMFWUPDATE_OBLOB "etc/vmfwupdate/opaque-blob"
#define FILE_VMFWUPDATE_FWBLOB "etc/vmfwupdate/fw-blob"
#define FILE_VMFWUPDATE_CAP "etc/vmfwupdate/cap"
#define FILE_VMFWUPDATE_BIOS_SIZE "etc/vmfwupdate/bios-size"
#define FILE_VMFWUPDATE_CONTROL "etc/vmfwupdate/disable"

/*
 * Address and length of the guest provided firmware blob.
 * The blob itself is passed using the guest shared memory to QEMU.
 * This is then copied to the guest private memeory in the secure vm
 * by the hypervisor.
 */
typedef struct {
    uint32_t bios_size; /*
                         * this is used by the guest to update plat_bios_size
                         * when VMFWUPDATE_CAP_BIOS_RESIZE is set.
                         */
    uint64_t bios_paddr; /*
                          * starting gpa where the blob is in shared guest
                          * memory. Cleared upon system reset.
                          */
} VMFwUpdateFwBlob;

typedef struct VMFwUpdateState {
    DeviceState parent_obj;

    /*
     * capabilities - 64 bits.
     * Little endian format.
     */
    uint64_t capability;

    /*
     * size of the bios region - architecture dependent.
     * Read-only by the guest unless VMFWUPDATE_CAP_BIOS_RESIZE
     * capability is set.
     */
    uint32_t plat_bios_size;

    /*
     * disable - disables the interface when non-zero value is written to it.
     * Writing 0 to this file enables the interface.
     */
    uint8_t disable;

    /*
     * The first stage boot uses this opaque blob to convey to the next stage
     * where the next stage components are loaded. The exact structure and
     * number of entries are unknown to the hypervisor and the hypervisor
     * does not touch this memory or do any validations.
     * The contents of this memory needs to be validated by the guest and
     * must be ABI compatible between the first and second stages.
     */
    unsigned char opaque_blobs[VMFWUPDATE_OPAQUE_SIZE];

    /*
     * firmware blob addresses and sizes. These are moved to guest
     * private memory.
     */
    VMFwUpdateFwBlob fw_blob;
} VMFwUpdateState;

OBJECT_DECLARE_SIMPLE_TYPE(VMFwUpdateState, VMFWUPDATE);

/* returns NULL unless there is exactly one device */
static inline VMFwUpdateState *vmfwupdate_find(void)
{
    Object *o = object_resolve_path_type("", TYPE_VMFWUPDATE, NULL);

    return o ? VMFWUPDATE(o) : NULL;
}

#endif
