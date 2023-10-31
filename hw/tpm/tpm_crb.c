/*
 * tpm_crb.c - QEMU's TPM CRB interface emulator
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * tpm_crb is a device for TPM 2.0 Command Response Buffer (CRB) Interface
 * as defined in TCG PC Client Platform TPM Profile (PTP) Specification
 * Family “2.0” Level 00 Revision 01.03 v22
 */

#include "qemu/osdep.h"

#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/acpi/tpm.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_ids.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "sysemu/tpm_backend.h"
#include "sysemu/tpm_util.h"
#include "sysemu/reset.h"
#include "sysemu/xen.h"
#include "tpm_prop.h"
#include "tpm_ppi.h"
#include "trace.h"
#include "qom/object.h"
#include "tpm_crb.h"

struct CRBState {
    DeviceState parent_obj;

    TPMCRBState state;

    /* These states are only for migration */
    uint32_t saved_regs[TPM_CRB_R_MAX];
    MemoryRegion saved_cmdmem;
};
typedef struct CRBState CRBState;

DECLARE_INSTANCE_CHECKER(CRBState, CRB,
                         TYPE_TPM_CRB)

static void tpm_crb_none_request_completed(TPMIf *ti, int ret)
{
    CRBState *s = CRB(ti);

    tpm_crb_request_completed(&s->state, ret);
}

static enum TPMVersion tpm_crb_none_get_version(TPMIf *ti)
{
    CRBState *s = CRB(ti);

    return tpm_crb_get_version(&s->state);
}

/**
 * For migrating to an older version of QEMU
 */
static int tpm_crb_none_pre_save(void *opaque)
{
    CRBState *s = opaque;
    void *saved_cmdmem = memory_region_get_ram_ptr(&s->saved_cmdmem);

    tpm_crb_mem_save(&s->state, s->saved_regs, saved_cmdmem);
    return tpm_crb_pre_save(&s->state);
}

/**
 * For migrating from an older version of QEMU
 */
static int tpm_crb_none_post_load(void *opaque, int version_id)
{
    CRBState *s = opaque;
    void *saved_cmdmem = memory_region_get_ram_ptr(&s->saved_cmdmem);

    tpm_crb_mem_load(&s->state, s->saved_regs, saved_cmdmem);
    return 0;
}

static const VMStateDescription vmstate_tpm_crb_none = {
    .name = "tpm-crb",
    .pre_save = tpm_crb_none_pre_save,
    .post_load = tpm_crb_none_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(saved_regs, CRBState, TPM_CRB_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property tpm_crb_none_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", CRBState, state.tpmbe),
    DEFINE_PROP_BOOL("ppi", CRBState, state.ppi_enabled, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_crb_none_reset(void *dev)
{
    CRBState *s = CRB(dev);

    return tpm_crb_reset(&s->state, TPM_CRB_ADDR_BASE);
}

static void tpm_crb_none_realize(DeviceState *dev, Error **errp)
{
    CRBState *s = CRB(dev);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }
    if (!s->state.tpmbe) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }

    if (tpm_crb_none_get_version(TPM_IF(s)) != TPM_VERSION_2_0) {
        error_setg(errp, "TPM CRB only supports TPM 2.0 backends");
        return;
    }

    tpm_crb_init_memory(OBJECT(s), &s->state, errp);

    /* only used for migration */
    memory_region_init_ram(&s->saved_cmdmem, OBJECT(s),
        "tpm-crb-cmd", CRB_CTRL_CMD_SIZE, errp);

    memory_region_add_subregion(get_system_memory(),
        TPM_CRB_ADDR_BASE, &s->state.mmio);

    if (s->state.ppi_enabled) {
        memory_region_add_subregion(get_system_memory(),
            TPM_PPI_ADDR_BASE, &s->state.ppi.ram);
    }

    if (xen_enabled()) {
        tpm_crb_none_reset(dev);
    } else {
        qemu_register_reset(tpm_crb_none_reset, dev);
    }
}

static void build_tpm_crb_none_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    tpm_crb_build_aml(TPM_IF(adev), scope, TPM_CRB_ADDR_BASE, TPM_CRB_ADDR_SIZE,
                      true);
}

static void tpm_crb_none_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    dc->realize = tpm_crb_none_realize;
    device_class_set_props(dc, tpm_crb_none_properties);
    dc->vmsd  = &vmstate_tpm_crb_none;
    dc->user_creatable = true;
    tc->model = TPM_MODEL_TPM_CRB;
    tc->get_version = tpm_crb_none_get_version;
    tc->request_completed = tpm_crb_none_request_completed;
    adevc->build_dev_aml = build_tpm_crb_none_aml;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo tpm_crb_none_info = {
    .name = TYPE_TPM_CRB,
    /* could be TYPE_SYS_BUS_DEVICE (or LPC etc) */
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CRBState),
    .class_init  = tpm_crb_none_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { TYPE_ACPI_DEV_AML_IF },
        { }
    }
};

static void tpm_crb_none_register(void)
{
    type_register_static(&tpm_crb_none_info);
}

type_init(tpm_crb_none_register)
