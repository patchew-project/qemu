#ifndef HW_COMPAT_H
#define HW_COMPAT_H

#define HW_COMPAT_2_7 \
    {\
        .driver   = "virtio-pci",\
        .property = "page-per-vq",\
        .value    = "on",\
    },{\
        .driver   = "virtio-serial-device",\
        .property = "emergency-write",\
        .value    = "off",\
    },{\
        .driver   = "ioapic",\
        .property = "version",\
        .value    = "0x11",\
    },{\
        .driver   = "intel-iommu",\
        .property = "x-buggy-eim",\
        .value    = "true",\
    },

#define HW_COMPAT_2_6 \
    {\
        .driver   = "virtio-mmio",\
        .property = "format_transport_address",\
        .value    = "off",\
    },{\
        .driver   = "virtio-scsi-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-scsi-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-blk-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-blk-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-balloon-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-balloon-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-serial-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-serial-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-net-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-net-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-9p-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-9p-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-rng-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-rng-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-input-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-input-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-input-hid-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-input-hid-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-keyboard-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-keyboard-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-mouse-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-mouse-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-tablet-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-tablet-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-input-host-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-input-host-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-gpu-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-gpu-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },{\
        .driver   = "virtio-crypto-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-crypto-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },

#define HW_COMPAT_2_5 \
    {\
        .driver   = "isa-fdc",\
        .property = "fallback",\
        .value    = "144",\
    },{\
        .driver   = "pvscsi",\
        .property = "x-old-pci-configuration",\
        .value    = "on",\
    },{\
        .driver   = "pvscsi",\
        .property = "x-disable-pcie",\
        .value    = "on",\
    },\
    {\
        .driver   = "vmxnet3",\
        .property = "x-old-msi-offsets",\
        .value    = "on",\
    },{\
        .driver   = "vmxnet3",\
        .property = "x-disable-pcie",\
        .value    = "on",\
    },

#define HW_COMPAT_2_4 \
    {\
        .driver   = "virtio-blk-device",\
        .property = "scsi",\
        .value    = "true",\
    },{\
        .driver   = "e1000",\
        .property = "extra_mac_registers",\
        .value    = "off",\
    },{\
        .driver   = "virtio-pci",\
        .property = "x-disable-pcie",\
        .value    = "on",\
    },{\
        .driver   = "virtio-pci",\
        .property = "migrate-extra",\
        .value    = "off",\
    },{\
        .driver   = "fw_cfg_mem",\
        .property = "dma_enabled",\
        .value    = "off",\
    },{\
        .driver   = "fw_cfg_io",\
        .property = "dma_enabled",\
        .value    = "off",\
    },

#define HW_COMPAT_2_3 \
    {\
        .driver   = "virtio-blk-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "virtio-balloon-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "virtio-serial-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "virtio-9p-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "virtio-rng-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = TYPE_PCI_DEVICE,\
        .property = "x-pcie-lnksta-dllla",\
        .value    = "off",\
    },

#define HW_COMPAT_2_2 \
    /* empty */

#define HW_COMPAT_2_1 \
    {\
        .driver   = "intel-hda",\
        .property = "old_msi_addr",\
        .value    = "on",\
    },{\
        .driver   = "VGA",\
        .property = "qemu-extended-regs",\
        .value    = "off",\
    },{\
        .driver   = "secondary-vga",\
        .property = "qemu-extended-regs",\
        .value    = "off",\
    },{\
        .driver   = "virtio-scsi-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "usb-mouse",\
        .property = "usb_version",\
        .value    = stringify(1),\
    },{\
        .driver   = "usb-kbd",\
        .property = "usb_version",\
        .value    = stringify(1),\
    },{\
        .driver   = "virtio-pci",\
        .property = "virtio-pci-bus-master-bug-migration",\
        .value    = "on",\
    },

#endif /* HW_COMPAT_H */
