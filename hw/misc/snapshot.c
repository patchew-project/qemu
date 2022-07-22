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

    // track saved stated to prevent re-saving
    bool is_saved;

    // saved cpu and devices state
    QIOChannelBuffer *ioc;
};

// memory save location (for better performance, use tmpfs)
const char *filepath = "/Volumes/RAMDisk/snapshot_0";

static void save_snapshot(struct SnapshotState *s) {
    if (s->is_saved) {
        return;
    }
    s->is_saved = true;

    // save memory state to file
    int fd = -1;
    uint8_t *guest_mem = current_machine->ram->ram_block->host;
    size_t guest_size = current_machine->ram->ram_block->max_length;

    fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
    ftruncate(fd, guest_size);

    char *map = mmap(0, guest_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memcpy(map, guest_mem, guest_size);
    msync(map, guest_size, MS_SYNC);
    munmap(map, guest_size);
    close(fd);

    // unmap the guest, we will now use a MAP_PRIVATE
    munmap(guest_mem, guest_size);

    // map as MAP_PRIVATE to avoid carrying writes back to the saved file
    fd = open(filepath, O_RDONLY);
    mmap(guest_mem, guest_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0);

    // save cpu and device state
    s->ioc = qemu_snapshot_save_cpu_state();
}

static void restore_snapshot(struct SnapshotState *s) {
    int fd = -1;
    uint8_t *guest_mem = current_machine->ram->ram_block->host;
    size_t guest_size = current_machine->ram->ram_block->max_length;

    if (!s->is_saved) {
        fprintf(stderr, "[QEMU] ERROR: attempting to restore but state has not been saved!\n");
        return;
    }

    munmap(guest_mem, guest_size);

    // remap the snapshot at the same location
    fd = open(filepath, O_RDONLY);
    mmap(guest_mem, guest_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0);
    close(fd);

    // restore cpu and device state
    qemu_snapshot_load_cpu_state(s->ioc);
}

static uint64_t snapshot_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void snapshot_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
    SnapshotState *snapshot = opaque;
    (void)snapshot;

    switch (addr) {
    case 0x00:
        switch (val) {
        case 0x101:
            save_snapshot(snapshot);
            break;
        case 0x102:
            restore_snapshot(snapshot);
            break;
        }
        break;
    }
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
    snapshot->is_saved = false;
    snapshot->ioc = NULL;

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
