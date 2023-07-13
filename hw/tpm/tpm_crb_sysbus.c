/*
 * tpm_crb_sysbus.c - QEMU's TPM CRB interface emulator
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
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/acpi/tpm.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "tpm_prop.h"
#include "hw/pci/pci_ids.h"
#include "hw/sysbus.h"
#include "qapi/visitor.h"
#include "qom/object.h"
#include "sysemu/tpm_util.h"
#include "trace.h"
#include "tpm_crb.h"

struct TPMCRBStateSysBus {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    TPMCRBState state;
    uint64_t baseaddr;
    uint64_t size;
};

OBJECT_DECLARE_SIMPLE_TYPE(TPMCRBStateSysBus, TPM_CRB_SYSBUS)

static void tpm_crb_sysbus_request_completed(TPMIf *ti, int ret)
{
    TPMCRBStateSysBus *s = TPM_CRB_SYSBUS(ti);

    return tpm_crb_request_completed(&s->state, ret);
}

static enum TPMVersion tpm_crb_sysbus_get_tpm_version(TPMIf *ti)
{
    TPMCRBStateSysBus *s = TPM_CRB_SYSBUS(ti);

    return tpm_crb_get_version(&s->state);
}

static int tpm_crb_sysbus_pre_save(void *opaque)
{
    TPMCRBStateSysBus *s = opaque;

    return tpm_crb_pre_save(&s->state);
}

static const VMStateDescription vmstate_tpm_crb_sysbus = {
    .name = "tpm-crb-sysbus",
    .pre_save = tpm_crb_sysbus_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static Property tpm_crb_sysbus_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", TPMCRBStateSysBus, state.tpmbe),
    DEFINE_PROP_BOOL("ppi", TPMCRBStateSysBus, state.ppi_enabled, false),
    DEFINE_PROP_UINT64("baseaddr", TPMCRBStateSysBus,
                       baseaddr, TPM_CRB_ADDR_BASE),
    DEFINE_PROP_UINT64("size", TPMCRBStateSysBus, size, TPM_CRB_ADDR_SIZE),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_crb_sysbus_initfn(Object *obj)
{
    TPMCRBStateSysBus *s = TPM_CRB_SYSBUS(obj);

    tpm_crb_init_memory(obj, &s->state, NULL);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->state.mmio);
}

static void tpm_crb_sysbus_reset(DeviceState *dev)
{
    TPMCRBStateSysBus *s = TPM_CRB_SYSBUS(dev);

    return tpm_crb_reset(&s->state, s->baseaddr);
}

static void tpm_crb_sysbus_realizefn(DeviceState *dev, Error **errp)
{
    TPMCRBStateSysBus *s = TPM_CRB_SYSBUS(dev);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    if (!s->state.tpmbe) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }

    if (s->state.ppi_enabled) {
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->state.ppi.ram);
    }
}

static void build_tpm_crb_sysbus_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *dev, *crs;
    TPMCRBStateSysBus *s = TPM_CRB_SYSBUS(adev);
    TPMIf *ti = TPM_IF(s);

    dev = aml_device("TPM");
    if (tpm_crb_sysbus_get_tpm_version(ti) == TPM_VERSION_2_0) {
        aml_append(dev, aml_name_decl("_HID", aml_string("MSFT0101")));
        aml_append(dev, aml_name_decl("_STR", aml_string("TPM 2.0 Device")));
    } else {
        aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0C31")));
    }
    aml_append(dev, aml_name_decl("_UID", aml_int(1)));
    aml_append(dev, aml_name_decl("_STA", aml_int(0xF)));
    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(s->baseaddr, s->size,
                                      AML_READ_WRITE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    /**
     * FIXME: PPI needs to also get a dynamic address.
     */
    /* tpm_build_ppi_acpi(ti, dev); */
    aml_append(scope, dev);
}

static void tpm_crb_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    device_class_set_props(dc, tpm_crb_sysbus_properties);
    dc->vmsd  = &vmstate_tpm_crb_sysbus;
    tc->model = TPM_MODEL_TPM_CRB;
    dc->realize = tpm_crb_sysbus_realizefn;
    dc->user_creatable = true;
    dc->reset = tpm_crb_sysbus_reset;
    tc->request_completed = tpm_crb_sysbus_request_completed;
    tc->get_version = tpm_crb_sysbus_get_tpm_version;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    adevc->build_dev_aml = build_tpm_crb_sysbus_aml;
}

static const TypeInfo tpm_crb_sysbus_info = {
    .name = TYPE_TPM_CRB_SYSBUS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TPMCRBStateSysBus),
    .instance_init = tpm_crb_sysbus_initfn,
    .class_init  = tpm_crb_sysbus_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { TYPE_ACPI_DEV_AML_IF },
        { }
    }
};

static void tpm_crb_sysbus_register(void)
{
    type_register_static(&tpm_crb_sysbus_info);
}

type_init(tpm_crb_sysbus_register)
