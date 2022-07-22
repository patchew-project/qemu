#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "exec/ramblock.h"
#include "qom/object.h"
#include "qemu/module.h"
#include "qapi/visitor.h"
#include "io/channel-buffer.h"
#include "migration/savevm.h"

#define TYPE_PCI_SNAPSHOT_DEVICE "snapshot"
typedef struct SnapshotState SnapshotState;
DECLARE_INSTANCE_CHECKER(SnapshotState, SNAPSHOT,
                         TYPE_PCI_SNAPSHOT_DEVICE)

struct SnapshotState {
    PCIDevice pdev;
    MemoryRegion mmio;
};

static uint64_t snapshot_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void snapshot_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
}

static const MemoryRegionOps snapshot_mmio_ops = {
    .read = snapshot_mmio_read,
    .write = snapshot_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },

};

static void pci_snapshot_realize(PCIDevice *pdev, Error **errp)
{
    SnapshotState *snapshot = SNAPSHOT(pdev);

    memory_region_init_io(&snapshot->mmio, OBJECT(snapshot), &snapshot_mmio_ops, snapshot,
                    "snapshot-mmio", 1 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &snapshot->mmio);
}

static void snapshot_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_snapshot_realize;
    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = 0xf987;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_snapshot_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo snapshot_info = {
        .name          = TYPE_PCI_SNAPSHOT_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(SnapshotState),
        .class_init    = snapshot_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&snapshot_info);
}
type_init(pci_snapshot_register_types)
