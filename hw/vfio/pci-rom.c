/*
 * vfio PCI device - ROM support
 *
 * Copyright Red Hat, Inc. 2012-2026
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>

#include "hw/core/hw-error.h"
#include "hw/pci/pci_device.h"
#include "qemu/error-report.h"
#include "pci.h"
#include "trace.h"
#include "qapi/error.h"
#include "vfio-helpers.h"

static bool vfio_pci_load_rom(VFIOPCIDevice *vdev, Error **errp)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    struct vfio_region_info *reg_info = NULL;
    uint64_t size;
    off_t off = 0;
    ssize_t bytes;
    int ret;

    ret = vfio_device_get_region_info(vbasedev, VFIO_PCI_ROM_REGION_INDEX,
                                      &reg_info);

    if (ret != 0) {
        error_setg_errno(errp, -ret, "vfio: Error getting ROM info");
        return false;
    }

    trace_vfio_pci_load_rom(vbasedev->name, (unsigned long)reg_info->size,
                            (unsigned long)reg_info->offset,
                            (unsigned long)reg_info->flags);

    vdev->rom_size = size = reg_info->size;
    vdev->rom_offset = reg_info->offset;

    if (!vdev->rom_size) {
        vdev->rom_size = 0;
        vdev->rom_offset = 0;
        error_setg(errp, "vfio-pci: Device ROM size is zero at %s",
                   vbasedev->name);
        error_append_hint(errp, "Device option ROM contents are probably "
                          "invalid (check dmesg).\nSkip option ROM probe "
                          "with rombar=0, or load from file with romfile=\n");
        return false;
    }

    vdev->rom = g_malloc(size);
    memset(vdev->rom, 0xff, size);

    while (size) {
        bytes = vbasedev->io_ops->region_read(vbasedev,
                                              VFIO_PCI_ROM_REGION_INDEX,
                                              off, size, vdev->rom + off);

        if (bytes == 0) {
            break;
        } else if (bytes > 0) {
            off += bytes;
            size -= bytes;
        } else {
            if (bytes == -EINTR || bytes == -EAGAIN) {
                continue;
            }
            error_setg_errno(errp, -bytes, "vfio: Error reading device ROM");
            g_free(vdev->rom);
            vdev->rom = NULL;
            vdev->rom_size = 0;
            vdev->rom_offset = 0;
            return false;
        }
    }

    /*
     * Test the ROM signature against our device, if the vendor is correct
     * but the device ID doesn't match, store the correct device ID and
     * recompute the checksum.  Intel IGD devices need this and are known
     * to have bogus checksums so we can't simply adjust the checksum.
     */
    if (pci_get_word(vdev->rom) == 0xaa55 &&
        pci_get_word(vdev->rom + 0x18) + 8 < vdev->rom_size &&
        !memcmp(vdev->rom + pci_get_word(vdev->rom + 0x18), "PCIR", 4)) {
        uint16_t vid, did;

        vid = pci_get_word(vdev->rom + pci_get_word(vdev->rom + 0x18) + 4);
        did = pci_get_word(vdev->rom + pci_get_word(vdev->rom + 0x18) + 6);

        if (vid == vdev->vendor_id && did != vdev->device_id) {
            int i;
            uint8_t csum, *data = vdev->rom;

            pci_set_word(vdev->rom + pci_get_word(vdev->rom + 0x18) + 6,
                         vdev->device_id);
            data[6] = 0;

            for (csum = 0, i = 0; i < vdev->rom_size; i++) {
                csum += data[i];
            }

            data[6] = -csum;
        }
    }

    vfio_rom_quirk_setup(vdev);

    return true;
}

static uint64_t vfio_rom_read(void *opaque, hwaddr addr, unsigned size)
{
    VFIOPCIDevice *vdev = opaque;
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } val = { .qword = ~0ULL };
    uint64_t data = 0;

    /* Load the ROM lazily when the guest tries to read it */
    if (unlikely(!vdev->rom && !vdev->rom_read_failed)) {
        Error *local_err = NULL;

        vdev->rom_read_failed = !vfio_pci_load_rom(vdev, &local_err);
        if (vdev->rom_read_failed) {
            error_report_err(local_err);
        }
    }

    memcpy(&val, vdev->rom + addr,
           (addr < vdev->rom_size) ? MIN(size, vdev->rom_size - addr) : 0);

    switch (size) {
    case 1:
        data = val.byte;
        break;
    case 2:
        data = le16_to_cpu(val.word);
        break;
    case 4:
        data = le32_to_cpu(val.dword);
        break;
    default:
        hw_error("vfio: unsupported read size, %d bytes\n", size);
        break;
    }

    trace_vfio_rom_read(vdev->vbasedev.name, addr, size, data);

    return data;
}

static void vfio_rom_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned size)
{
}

static const MemoryRegionOps vfio_rom_ops = {
    .read = vfio_rom_read,
    .write = vfio_rom_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void vfio_pci_size_rom(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    uint32_t orig, size = cpu_to_le32((uint32_t)PCI_ROM_ADDRESS_MASK);
    char *name;

    if (pdev->romfile || !pdev->rom_bar) {
        /* Since pci handles romfile, just print a message and return */
        if (vfio_opt_rom_in_denylist(vdev) && pdev->romfile) {
            warn_report("Device at %s is known to cause system instability"
                        " issues during option rom execution",
                        vdev->vbasedev.name);
            error_printf("Proceeding anyway since user specified romfile\n");
        }
        return;
    }

    /*
     * Use the same size ROM BAR as the physical device.  The contents
     * will get filled in later when the guest tries to read it.
     */
    if (vfio_pci_config_space_read(vdev, PCI_ROM_ADDRESS, 4, &orig) != 4 ||
        vfio_pci_config_space_write(vdev, PCI_ROM_ADDRESS, 4, &size) != 4 ||
        vfio_pci_config_space_read(vdev, PCI_ROM_ADDRESS, 4, &size) != 4 ||
        vfio_pci_config_space_write(vdev, PCI_ROM_ADDRESS, 4, &orig) != 4) {

        error_report("%s(%s) ROM access failed", __func__, vbasedev->name);
        return;
    }

    size = ~(le32_to_cpu(size) & PCI_ROM_ADDRESS_MASK) + 1;

    if (!size) {
        return;
    }

    if (vfio_opt_rom_in_denylist(vdev)) {
        if (pdev->rom_bar > 0) {
            warn_report("Device at %s is known to cause system instability"
                        " issues during option rom execution",
                        vdev->vbasedev.name);
            error_printf("Proceeding anyway since user specified"
                         " positive value for rombar\n");
        } else {
            warn_report("Rom loading for device at %s has been disabled"
                        " due to system instability issues",
                        vdev->vbasedev.name);
            error_printf("Specify rombar=1 or romfile to force\n");
            return;
        }
    }

    trace_vfio_pci_size_rom(vdev->vbasedev.name, size);

    name = g_strdup_printf("vfio[%s].rom", vdev->vbasedev.name);

    memory_region_init_io(&pdev->rom, OBJECT(vdev),
                          &vfio_rom_ops, vdev, name, size);
    g_free(name);

    pci_register_bar(pdev, PCI_ROM_SLOT,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &pdev->rom);

    vdev->rom_read_failed = false;
}
