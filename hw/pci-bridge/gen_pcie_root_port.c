/*
 * Generic PCI Express Root Port emulation
 *
 * Copyright (C) 2017 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie_port.h"

#define TYPE_GEN_PCIE_ROOT_PORT                "pcie-root-port"

#define GEN_PCIE_ROOT_PORT_AER_OFFSET           0x100
#define GEN_PCIE_ROOT_PORT_MSIX_NR_VECTOR       1

#define GEN_PCIE_ROOT_PORT(obj) \
     OBJECT_CHECK(GenPCIERootPort, (obj), TYPE_GEN_PCIE_ROOT_PORT)
#define GEN_PCIE_ROOT_PORT_CLASS(klass) \
     OBJECT_CLASS_CHECK(GenPCIERootPortClass, (klass), TYPE_GEN_PCIE_ROOT_PORT)
#define GEN_PCIE_ROOT_PORT_GET_CLASS(obj) \
     OBJECT_GET_CLASS(GenPCIERootPortClass, (obj), TYPE_GEN_PCIE_ROOT_PORT)

typedef struct GenPCIERootPortClass {
    PCIERootPortClass parent_class;

    DeviceRealize parent_realize;
} GenPCIERootPortClass;

typedef struct GenPCIERootPort {
    /*< private >*/
    PCIESlot parent_obj;
    /*< public >*/

    bool migrate_msix;
    bool enable_io_fwd;
} GenPCIERootPort;

static uint8_t gen_rp_aer_vector(const PCIDevice *d)
{
    return 0;
}

static int gen_rp_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msix_init_exclusive_bar(d, GEN_PCIE_ROOT_PORT_MSIX_NR_VECTOR, 0, errp);

    if (rc < 0) {
        assert(rc == -ENOTSUP);
    } else {
        msix_vector_use(d, 0);
    }

    return rc;
}

static void gen_rp_interrupts_uninit(PCIDevice *d)
{
    msix_uninit_exclusive_bar(d);
}

static bool gen_rp_test_migrate_msix(void *opaque, int version_id)
{
    GenPCIERootPort *rp = opaque;

    return rp->migrate_msix;
}

static void gen_rp_realize(DeviceState *d, Error **errp)
{
    GenPCIERootPortClass *grpc = GEN_PCIE_ROOT_PORT_GET_CLASS(d);
    GenPCIERootPort *grp = GEN_PCIE_ROOT_PORT(d);
    PCIDevice *pci_dev = PCI_DEVICE(d);

    grpc->parent_realize(DEVICE(d), errp);
    if (*errp) {
        return;
    }

    if (!grp->enable_io_fwd) {
        pci_word_test_and_clear_mask(pci_dev->wmask + PCI_COMMAND,
                                     PCI_COMMAND_IO);
        pci_dev->wmask[PCI_IO_BASE] = 0;
        pci_dev->wmask[PCI_IO_LIMIT] = 0;
    }
}

static const VMStateDescription vmstate_rp_dev = {
    .name = "pcie-root-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_MSIX_TEST(parent_obj.parent_obj.parent_obj.parent_obj,
                          GenPCIERootPort,
                          gen_rp_test_migrate_msix),
        VMSTATE_END_OF_LIST()
    }
};

static Property gen_rp_props[] = {
    DEFINE_PROP_BOOL("x-migrate-msix", GenPCIERootPort, migrate_msix, true),
    DEFINE_PROP_BOOL("enable-io-fwd", GenPCIERootPort, enable_io_fwd, false),
    DEFINE_PROP_END_OF_LIST()
};

static void gen_rp_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);
    GenPCIERootPortClass *grpc = GEN_PCIE_ROOT_PORT_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_RP;
    dc->desc = "PCI Express Root Port";
    dc->vmsd = &vmstate_rp_dev;
    dc->props = gen_rp_props;
    rpc->aer_vector = gen_rp_aer_vector;
    rpc->interrupts_init = gen_rp_interrupts_init;
    rpc->interrupts_uninit = gen_rp_interrupts_uninit;
    rpc->aer_offset = GEN_PCIE_ROOT_PORT_AER_OFFSET;
    grpc->parent_realize = dc->realize;
    dc->realize = gen_rp_realize;
}

static const TypeInfo gen_rp_dev_info = {
    .name          = TYPE_GEN_PCIE_ROOT_PORT,
    .parent        = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(GenPCIERootPort),
    .class_init    = gen_rp_dev_class_init,
    .class_size    = sizeof(GenPCIERootPortClass),
};

 static void gen_rp_register_types(void)
 {
    type_register_static(&gen_rp_dev_info);
 }
 type_init(gen_rp_register_types)
