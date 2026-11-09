/*
 * vfio based device assignment support
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Alex Williamson <alex.williamson@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Based on qemu-kvm device-assignment:
 *  Adapted for KVM by Qumranet.
 *  Copyright (c) 2007, Neocleus, Alex Novik (alex@neocleus.com)
 *  Copyright (c) 2007, Neocleus, Guy Zana (guy@neocleus.com)
 *  Copyright (C) 2008, Qumranet, Amit Shah (amit.shah@qumranet.com)
 *  Copyright (C) 2008, Red Hat, Amit Shah (amit.shah@redhat.com)
 *  Copyright (C) 2008, IBM, Muli Ben-Yehuda (muli@il.ibm.com)
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "hw/core/hw-error.h"
#include "hw/core/iommu.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_bridge.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/vfio/vfio-cpr.h"
#include "migration/vmstate.h"
#include "migration/cpr.h"
#include "qobject/qdict.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "qemu/units.h"
#include "system/kvm.h"
#include "pci.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/blocker.h"
#include "migration/qemu-file.h"
#include "system/iommufd.h"
#include "vfio-migration-internal.h"
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

/* "Raw" read of underlying config space. */
int vfio_pci_config_space_read(VFIOPCIDevice *vdev, off_t offset,
                               uint32_t size, void *data)
{
    return vdev->vbasedev.io_ops->region_read(&vdev->vbasedev,
                                              VFIO_PCI_CONFIG_REGION_INDEX,
                                              offset, size, data);
}

/* "Raw" write of underlying config space. */
int vfio_pci_config_space_write(VFIOPCIDevice *vdev, off_t offset,
                                uint32_t size, void *data)
{
    return vdev->vbasedev.io_ops->region_write(&vdev->vbasedev,
                                               VFIO_PCI_CONFIG_REGION_INDEX,
                                               offset, size, data, false);
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

static void vfio_pci_size_rom(VFIOPCIDevice *vdev)
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

void vfio_vga_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned size)
{
    VFIOVGARegion *region = opaque;
    VFIOVGA *vga = container_of(region, VFIOVGA, region[region->nr]);
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } buf;
    off_t offset = vga->fd_offset + region->offset + addr;

    switch (size) {
    case 1:
        buf.byte = data;
        break;
    case 2:
        buf.word = cpu_to_le16(data);
        break;
    case 4:
        buf.dword = cpu_to_le32(data);
        break;
    default:
        hw_error("vfio: unsupported write size, %d bytes", size);
        break;
    }

    if (pwrite(vga->fd, &buf, size, offset) != size) {
        error_report("%s(,0x%"HWADDR_PRIx", 0x%"PRIx64", %d) failed: %m",
                     __func__, region->offset + addr, data, size);
    }

    trace_vfio_vga_write(region->offset + addr, data, size);
}

uint64_t vfio_vga_read(void *opaque, hwaddr addr, unsigned size)
{
    VFIOVGARegion *region = opaque;
    VFIOVGA *vga = container_of(region, VFIOVGA, region[region->nr]);
    union {
        uint8_t byte;
        uint16_t word;
        uint32_t dword;
        uint64_t qword;
    } buf;
    uint64_t data = 0;
    off_t offset = vga->fd_offset + region->offset + addr;

    if (pread(vga->fd, &buf, size, offset) != size) {
        error_report("%s(,0x%"HWADDR_PRIx", %d) failed: %m",
                     __func__, region->offset + addr, size);
        return (uint64_t)-1;
    }

    switch (size) {
    case 1:
        data = buf.byte;
        break;
    case 2:
        data = le16_to_cpu(buf.word);
        break;
    case 4:
        data = le32_to_cpu(buf.dword);
        break;
    default:
        hw_error("vfio: unsupported read size, %d bytes", size);
        break;
    }

    trace_vfio_vga_read(region->offset + addr, size, data);

    return data;
}

static const MemoryRegionOps vfio_vga_ops = {
    .read = vfio_vga_read,
    .write = vfio_vga_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * Expand memory region of sub-page(size < PAGE_SIZE) MMIO BAR to page
 * size if the BAR is in an exclusive page in host so that we could map
 * this BAR to guest. But this sub-page BAR may not occupy an exclusive
 * page in guest. So we should set the priority of the expanded memory
 * region to zero in case of overlap with BARs which share the same page
 * with the sub-page BAR in guest. Besides, we should also recover the
 * size of this sub-page BAR when its base address is changed in guest
 * and not page aligned any more.
 */
static void vfio_sub_page_bar_update_mapping(PCIDevice *pdev, int bar)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
    VFIORegion *region = &vdev->bars[bar].region;
    MemoryRegion *mmap_mr, *region_mr, *base_mr;
    PCIIORegion *r;
    pcibus_t bar_addr;
    uint64_t size = region->size;

    /* Make sure that the whole region is allowed to be mmapped */
    if (region->nr_mmaps != 1 || !region->mmaps[0].mmap ||
        region->mmaps[0].size != region->size) {
        return;
    }

    r = &pdev->io_regions[bar];
    bar_addr = r->addr;
    base_mr = vdev->bars[bar].mr;
    region_mr = region->mem;
    mmap_mr = &region->mmaps[0].mem;

    /* If BAR is mapped and page aligned, update to fill PAGE_SIZE */
    if (bar_addr != PCI_BAR_UNMAPPED &&
        !(bar_addr & ~qemu_real_host_page_mask())) {
        size = qemu_real_host_page_size();
    }

    memory_region_transaction_begin();

    if (vdev->bars[bar].size < size) {
        memory_region_set_size(base_mr, size);
    }
    memory_region_set_size(region_mr, size);
    memory_region_set_size(mmap_mr, size);
    if (size != vdev->bars[bar].size && memory_region_is_mapped(base_mr)) {
        memory_region_del_subregion(r->address_space, base_mr);
        memory_region_add_subregion_overlap(r->address_space,
                                            bar_addr, base_mr, 0);
    }

    memory_region_transaction_commit();
}

/*
 * PCI config space
 */
uint32_t vfio_pci_read_config(PCIDevice *pdev, uint32_t addr, int len)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    uint32_t emu_bits = 0, emu_val = 0, phys_val = 0, val;

    memcpy(&emu_bits, vdev->emulated_config_bits + addr, len);
    emu_bits = le32_to_cpu(emu_bits);

    if (emu_bits) {
        emu_val = pci_default_read_config(pdev, addr, len);
    }

    if (~emu_bits & (0xffffffffU >> (32 - len * 8))) {
        ssize_t ret;

        ret = vfio_pci_config_space_read(vdev, addr, len, &phys_val);
        if (ret != len) {
            error_report("%s(%s, 0x%x, 0x%x) failed: %s",
                         __func__, vbasedev->name, addr, len,
                         strreaderror(ret));
            return -1;
        }
        phys_val = le32_to_cpu(phys_val);
    }

    val = (emu_val & emu_bits) | (phys_val & ~emu_bits);

    trace_vfio_pci_read_config(vdev->vbasedev.name, addr, len, val);

    return val;
}

void vfio_pci_write_config(PCIDevice *pdev,
                           uint32_t addr, uint32_t val, int len)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    uint32_t val_le = cpu_to_le32(val);
    int ret;

    trace_vfio_pci_write_config(vdev->vbasedev.name, addr, val, len);

    /* Write everything to VFIO, let it filter out what we can't write */
    ret = vfio_pci_config_space_write(vdev, addr, len, &val_le);
    if (ret != len) {
        error_report("%s(%s, 0x%x, 0x%x, 0x%x) failed: %s",
                     __func__, vbasedev->name, addr, val, len,
                    strwriteerror(ret));
    }

    /* MSI/MSI-X Enabling/Disabling */
    if (pdev->cap_present & QEMU_PCI_CAP_MSI &&
        ranges_overlap(addr, len, pdev->msi_cap, vdev->msi_cap_size)) {
        int is_enabled, was_enabled = msi_enabled(pdev);

        pci_default_write_config(pdev, addr, val, len);

        is_enabled = msi_enabled(pdev);

        if (!was_enabled) {
            if (is_enabled) {
                vfio_pci_msi_enable(vdev);
            }
        } else {
            if (!is_enabled) {
                vfio_pci_msi_disable(vdev);
            } else {
                vfio_pci_msi_update(vdev);
            }
        }
    } else if (pdev->cap_present & QEMU_PCI_CAP_MSIX &&
        ranges_overlap(addr, len, pdev->msix_cap, MSIX_CAP_LENGTH)) {
        int is_enabled, was_enabled = msix_enabled(pdev);

        pci_default_write_config(pdev, addr, val, len);

        is_enabled = msix_enabled(pdev);

        if (!was_enabled && is_enabled) {
            vfio_pci_msix_enable(vdev);
        } else if (was_enabled && !is_enabled) {
            vfio_pci_msix_disable(vdev);
        }
    } else if (ranges_overlap(addr, len, PCI_BASE_ADDRESS_0, 24) ||
        range_covers_byte(addr, len, PCI_COMMAND)) {
        pcibus_t old_addr[PCI_NUM_REGIONS - 1];
        int bar;

        for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
            old_addr[bar] = pdev->io_regions[bar].addr;
        }

        pci_default_write_config(pdev, addr, val, len);

        for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
            if (old_addr[bar] != pdev->io_regions[bar].addr &&
                vdev->bars[bar].region.size > 0 &&
                vdev->bars[bar].region.size < qemu_real_host_page_size()) {
                vfio_sub_page_bar_update_mapping(pdev, bar);
            }
        }
    } else {
        /* Write everything to QEMU to keep emulated bits correct */
        pci_default_write_config(pdev, addr, val, len);
    }
}

static void vfio_bar_prepare(VFIOPCIDevice *vdev, int nr)
{
    VFIOBAR *bar = &vdev->bars[nr];

    uint32_t pci_bar;
    int ret;

    /* Skip both unimplemented BARs and the upper half of 64bit BARS. */
    if (!bar->region.size) {
        return;
    }

    /* Determine what type of BAR this is for registration */
    ret = vfio_pci_config_space_read(vdev, PCI_BASE_ADDRESS_0 + (4 * nr),
                                     sizeof(pci_bar), &pci_bar);
    if (ret != sizeof(pci_bar)) {
        error_report("vfio: Failed to read BAR %d: %s", nr, strreaderror(ret));
        return;
    }

    pci_bar = le32_to_cpu(pci_bar);
    bar->ioport = (pci_bar & PCI_BASE_ADDRESS_SPACE_IO);
    bar->mem64 = bar->ioport ? 0 : (pci_bar & PCI_BASE_ADDRESS_MEM_TYPE_64);
    bar->type = pci_bar & (bar->ioport ? ~PCI_BASE_ADDRESS_IO_MASK :
                                         ~PCI_BASE_ADDRESS_MEM_MASK);
    bar->size = bar->region.size;

    /* IO regions are sync, memory can be async */
    bar->region.post_wr = (bar->ioport == 0);
}

static void vfio_bars_prepare(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        vfio_bar_prepare(vdev, i);
    }
}

static void vfio_bar_register(VFIOPCIDevice *vdev, int nr)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    VFIOBAR *bar = &vdev->bars[nr];
    char *name;

    if (!bar->size) {
        return;
    }

    bar->mr = g_new0(MemoryRegion, 1);
    name = g_strdup_printf("%s base BAR %d", vdev->vbasedev.name, nr);
    memory_region_init_io(bar->mr, OBJECT(vdev), NULL, NULL, name, bar->size);
    g_free(name);

    if (bar->region.size) {
        memory_region_add_subregion(bar->mr, 0, bar->region.mem);

        if (vfio_region_mmap(&bar->region)) {
            error_report("Failed to mmap %s BAR %d. Performance may be slow",
                         vdev->vbasedev.name, nr);
        }
    }

    pci_register_bar(pdev, nr, bar->type, bar->mr);
}

static void vfio_bars_register(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        vfio_bar_register(vdev, i);
    }
}

void vfio_pci_bars_exit(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        VFIOBAR *bar = &vdev->bars[i];

        vfio_bar_quirk_exit(vdev, i);
        vfio_region_exit(&bar->region);
        if (bar->region.size) {
            memory_region_del_subregion(bar->mr, bar->region.mem);
        }
    }

    if (vdev->vga) {
        pci_unregister_vga(pdev);
        vfio_vga_quirk_exit(vdev);
    }
}

static void vfio_bars_finalize(VFIOPCIDevice *vdev)
{
    int i;

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        VFIOBAR *bar = &vdev->bars[i];

        vfio_bar_quirk_finalize(vdev, i);
        vfio_region_finalize(&bar->region);
        if (bar->mr) {
            assert(bar->size);
            g_free(bar->mr);
            bar->mr = NULL;
        }
    }

    if (vdev->vga) {
        vfio_vga_quirk_finalize(vdev);
        g_free(vdev->vga);
    }
}

/*
 * General setup
 */
static uint8_t vfio_std_cap_max_size(PCIDevice *pdev, uint8_t pos)
{
    uint8_t tmp;
    uint16_t next = PCI_CONFIG_SPACE_SIZE;

    for (tmp = pdev->config[PCI_CAPABILITY_LIST]; tmp;
         tmp = pdev->config[tmp + PCI_CAP_LIST_NEXT]) {
        if (tmp > pos && tmp < next) {
            next = tmp;
        }
    }

    return next - pos;
}


static uint16_t vfio_ext_cap_max_size(const uint8_t *config, uint16_t pos)
{
    uint16_t tmp, next = PCIE_CONFIG_SPACE_SIZE;

    for (tmp = PCI_CONFIG_SPACE_SIZE; tmp;
        tmp = PCI_EXT_CAP_NEXT(pci_get_long(config + tmp))) {
        if (tmp > pos && tmp < next) {
            next = tmp;
        }
    }

    return next - pos;
}

static void vfio_set_word_bits(uint8_t *buf, uint16_t val, uint16_t mask)
{
    pci_set_word(buf, (pci_get_word(buf) & ~mask) | val);
}

static void vfio_add_emulated_word(VFIOPCIDevice *vdev, int pos,
                                   uint16_t val, uint16_t mask)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    vfio_set_word_bits(pdev->config + pos, val, mask);
    vfio_set_word_bits(pdev->wmask + pos, ~mask, mask);
    vfio_set_word_bits(vdev->emulated_config_bits + pos, mask, mask);
}

static void vfio_set_long_bits(uint8_t *buf, uint32_t val, uint32_t mask)
{
    pci_set_long(buf, (pci_get_long(buf) & ~mask) | val);
}

static void vfio_add_emulated_long(VFIOPCIDevice *vdev, int pos,
                                   uint32_t val, uint32_t mask)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    vfio_set_long_bits(pdev->config + pos, val, mask);
    vfio_set_long_bits(pdev->wmask + pos, ~mask, mask);
    vfio_set_long_bits(vdev->emulated_config_bits + pos, mask, mask);
}

static void vfio_pci_enable_rp_atomics(VFIOPCIDevice *vdev)
{
    struct vfio_device_info_cap_pci_atomic_comp *cap;
    g_autofree struct vfio_device_info *info = NULL;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    PCIBus *bus = pci_get_bus(pdev);
    PCIDevice *parent = bus->parent_dev;
    struct vfio_info_cap_header *hdr;
    uint32_t mask = 0;
    uint8_t *pos;

    /*
     * PCIe Atomic Ops completer support is only added automatically for single
     * function devices downstream of a root port supporting DEVCAP2.  Support
     * is added during realize and, if added, removed during device exit.  The
     * single function requirement avoids conflicting requirements should a
     * slot be composed of multiple devices with differing capabilities.
     */
    if (pci_bus_is_root(bus) || !parent || !parent->exp.exp_cap ||
        pcie_cap_get_type(parent) != PCI_EXP_TYPE_ROOT_PORT ||
        pcie_cap_get_version(parent) != PCI_EXP_FLAGS_VER2 ||
        pdev->devfn ||
        pdev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        return;
    }

    pos = parent->config + parent->exp.exp_cap + PCI_EXP_DEVCAP2;

    /* Abort if there'a already an Atomic Ops configuration on the root port */
    if (pci_get_long(pos) & (PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
                             PCI_EXP_DEVCAP2_ATOMIC_COMP64 |
                             PCI_EXP_DEVCAP2_ATOMIC_COMP128)) {
        return;
    }

    info = vfio_get_device_info(vdev->vbasedev.fd);
    if (!info) {
        return;
    }

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_PCI_ATOMIC_COMP);
    if (!hdr) {
        return;
    }

    cap = (void *)hdr;
    if (cap->flags & VFIO_PCI_ATOMIC_COMP32) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP32;
    }
    if (cap->flags & VFIO_PCI_ATOMIC_COMP64) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP64;
    }
    if (cap->flags & VFIO_PCI_ATOMIC_COMP128) {
        mask |= PCI_EXP_DEVCAP2_ATOMIC_COMP128;
    }

    if (!mask) {
        return;
    }

    pci_long_test_and_set_mask(pos, mask);
    vdev->clear_parent_atomics_on_exit = true;
}

static void vfio_pci_disable_rp_atomics(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    if (vdev->clear_parent_atomics_on_exit) {
        PCIDevice *parent = pci_get_bus(pdev)->parent_dev;
        uint8_t *pos = parent->config + parent->exp.exp_cap + PCI_EXP_DEVCAP2;

        pci_long_test_and_clear_mask(pos, PCI_EXP_DEVCAP2_ATOMIC_COMP32 |
                                          PCI_EXP_DEVCAP2_ATOMIC_COMP64 |
                                          PCI_EXP_DEVCAP2_ATOMIC_COMP128);
    }
}

static bool vfio_setup_pcie_cap(VFIOPCIDevice *vdev, int pos, uint8_t size,
                                Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint16_t flags;
    uint8_t type;

    flags = pci_get_word(pdev->config + pos + PCI_CAP_FLAGS);
    type = (flags & PCI_EXP_FLAGS_TYPE) >> 4;

    if (type != PCI_EXP_TYPE_ENDPOINT &&
        type != PCI_EXP_TYPE_LEG_END &&
        type != PCI_EXP_TYPE_RC_END) {

        error_setg(errp, "assignment of PCIe type 0x%x "
                   "devices is not currently supported", type);
        return false;
    }

    if (!pci_bus_is_express(pci_get_bus(pdev))) {
        PCIBus *bus = pci_get_bus(pdev);
        PCIDevice *bridge;

        /*
         * Traditionally PCI device assignment exposes the PCIe capability
         * as-is on non-express buses.  The reason being that some drivers
         * simply assume that it's there, for example tg3.  However when
         * we're running on a native PCIe machine type, like Q35, we need
         * to hide the PCIe capability.  The reason for this is twofold;
         * first Windows guests get a Code 10 error when the PCIe capability
         * is exposed in this configuration.  Therefore express devices won't
         * work at all unless they're attached to express buses in the VM.
         * Second, a native PCIe machine introduces the possibility of fine
         * granularity IOMMUs supporting both translation and isolation.
         * Guest code to discover the IOMMU visibility of a device, such as
         * IOMMU grouping code on Linux, is very aware of device types and
         * valid transitions between bus types.  An express device on a non-
         * express bus is not a valid combination on bare metal systems.
         *
         * Drivers that require a PCIe capability to make the device
         * functional are simply going to need to have their devices placed
         * on a PCIe bus in the VM.
         */
        while (!pci_bus_is_root(bus)) {
            bridge = pci_bridge_get_device(bus);
            bus = pci_get_bus(bridge);
        }

        if (pci_bus_is_express(bus)) {
            return true;
        }

    } else if (pci_bus_is_root(pci_get_bus(pdev))) {
        /*
         * On a Root Complex bus Endpoints become Root Complex Integrated
         * Endpoints, which changes the type and clears the LNK & LNK2 fields.
         */
        if (type == PCI_EXP_TYPE_ENDPOINT) {
            vfio_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                                   PCI_EXP_TYPE_RC_END << 4,
                                   PCI_EXP_FLAGS_TYPE);

            /* Link Capabilities, Status, and Control goes away */
            if (size > PCI_EXP_LNKCTL) {
                vfio_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP, 0, ~0);
                vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL, 0, ~0);
                vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKSTA, 0, ~0);

#ifndef PCI_EXP_LNKCAP2
#define PCI_EXP_LNKCAP2 44
#endif
#ifndef PCI_EXP_LNKSTA2
#define PCI_EXP_LNKSTA2 50
#endif
                /* Link 2 Capabilities, Status, and Control goes away */
                if (size > PCI_EXP_LNKCAP2) {
                    vfio_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP2, 0, ~0);
                    vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL2, 0, ~0);
                    vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKSTA2, 0, ~0);
                }
            }

        } else if (type == PCI_EXP_TYPE_LEG_END) {
            /*
             * Legacy endpoints don't belong on the root complex.  Windows
             * seems to be happier with devices if we skip the capability.
             */
            return true;
        }

    } else {
        /*
         * Convert Root Complex Integrated Endpoints to regular endpoints.
         * These devices don't support LNK/LNK2 capabilities, so make them up.
         */
        if (type == PCI_EXP_TYPE_RC_END) {
            vfio_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                                   PCI_EXP_TYPE_ENDPOINT << 4,
                                   PCI_EXP_FLAGS_TYPE);
            vfio_add_emulated_long(vdev, pos + PCI_EXP_LNKCAP,
                           QEMU_PCI_EXP_LNKCAP_MLW(QEMU_PCI_EXP_LNK_X1) |
                           QEMU_PCI_EXP_LNKCAP_MLS(QEMU_PCI_EXP_LNK_2_5GT), ~0);
            vfio_add_emulated_word(vdev, pos + PCI_EXP_LNKCTL, 0, ~0);
        }

        vfio_pci_enable_rp_atomics(vdev);
    }

    /*
     * Intel 82599 SR-IOV VFs report an invalid PCIe capability version 0
     * (Niantic errate #35) causing Windows to error with a Code 10 for the
     * device on Q35.  Fixup any such devices to report version 1.  If we
     * were to remove the capability entirely the guest would lose extended
     * config space.
     */
    if ((flags & PCI_EXP_FLAGS_VERS) == 0) {
        vfio_add_emulated_word(vdev, pos + PCI_CAP_FLAGS,
                               1, PCI_EXP_FLAGS_VERS);
    }

    pos = pci_add_capability(pdev, PCI_CAP_ID_EXP, pos, size, errp);
    if (pos < 0) {
        return false;
    }

    pdev->exp.exp_cap = pos;

    return true;
}

static void vfio_check_pcie_flr(VFIOPCIDevice *vdev, uint8_t pos)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint32_t cap = pci_get_long(pdev->config + pos + PCI_EXP_DEVCAP);

    if (cap & PCI_EXP_DEVCAP_FLR) {
        trace_vfio_check_pcie_flr(vdev->vbasedev.name);
        vdev->has_flr = true;
    }
}

static void vfio_check_pm_reset(VFIOPCIDevice *vdev, uint8_t pos)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint16_t csr = pci_get_word(pdev->config + pos + PCI_PM_CTRL);

    if (!(csr & PCI_PM_CTRL_NO_SOFT_RESET)) {
        trace_vfio_check_pm_reset(vdev->vbasedev.name);
        vdev->has_pm_reset = true;
    }
}

static void vfio_check_af_flr(VFIOPCIDevice *vdev, uint8_t pos)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint8_t cap = pci_get_byte(pdev->config + pos + PCI_AF_CAP);

    if ((cap & PCI_AF_CAP_TP) && (cap & PCI_AF_CAP_FLR)) {
        trace_vfio_check_af_flr(vdev->vbasedev.name);
        vdev->has_flr = true;
    }
}

static bool vfio_add_vendor_specific_cap(VFIOPCIDevice *vdev, int pos,
                                         uint8_t size, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);

    pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, pos, size, errp);
    if (pos < 0) {
        return false;
    }

    /*
     * Exempt config space check for Vendor Specific Information during
     * restore/load.
     * Config space check is still enforced for 3 byte VSC header.
     */
    if (vdev->skip_vsc_check && size > 3) {
        memset(pdev->cmask + pos + 3, 0, size - 3);
    }

    return true;
}

static bool vfio_add_std_cap(VFIOPCIDevice *vdev, uint8_t pos, Error **errp)
{
    ERRP_GUARD();
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint8_t cap_id, next, size;
    bool ret;

    cap_id = pdev->config[pos];
    next = pdev->config[pos + PCI_CAP_LIST_NEXT];

    /*
     * If it becomes important to configure capabilities to their actual
     * size, use this as the default when it's something we don't recognize.
     * Since QEMU doesn't actually handle many of the config accesses,
     * exact size doesn't seem worthwhile.
     */
    size = vfio_std_cap_max_size(pdev, pos);

    /*
     * pci_add_capability always inserts the new capability at the head
     * of the chain.  Therefore to end up with a chain that matches the
     * physical device, we insert from the end by making this recursive.
     * This is also why we pre-calculate size above as cached config space
     * will be changed as we unwind the stack.
     */
    if (next) {
        if (!vfio_add_std_cap(vdev, next, errp)) {
            return false;
        }
    } else {
        /* Begin the rebuild, use QEMU emulated list bits */
        pdev->config[PCI_CAPABILITY_LIST] = 0;
        vdev->emulated_config_bits[PCI_CAPABILITY_LIST] = 0xff;
        vdev->emulated_config_bits[PCI_STATUS] |= PCI_STATUS_CAP_LIST;

        if (!vfio_add_virt_caps(vdev, errp)) {
            return false;
        }
    }

    /* Scale down size, esp in case virt caps were added above */
    size = MIN(size, vfio_std_cap_max_size(pdev, pos));

    /* Use emulated next pointer to allow dropping caps */
    pci_set_byte(vdev->emulated_config_bits + pos + PCI_CAP_LIST_NEXT, 0xff);

    switch (cap_id) {
    case PCI_CAP_ID_MSI:
        ret = vfio_pci_msi_setup(vdev, pos, errp);
        break;
    case PCI_CAP_ID_EXP:
        vfio_check_pcie_flr(vdev, pos);
        ret = vfio_setup_pcie_cap(vdev, pos, size, errp);
        break;
    case PCI_CAP_ID_MSIX:
        ret = vfio_pci_msix_setup(vdev, pos, errp);
        break;
    case PCI_CAP_ID_PM:
        vfio_check_pm_reset(vdev, pos);
        ret = pci_pm_init(pdev, pos, errp) >= 0;
        /*
         * PCI-core config space emulation needs write access to the power
         * state enabled for tracking BAR mapping relative to PM state.
         */
        pci_set_word(pdev->wmask + pos + PCI_PM_CTRL, PCI_PM_CTRL_STATE_MASK);
        break;
    case PCI_CAP_ID_AF:
        vfio_check_af_flr(vdev, pos);
        ret = pci_add_capability(pdev, cap_id, pos, size, errp) >= 0;
        break;
    case PCI_CAP_ID_VNDR:
        ret = vfio_add_vendor_specific_cap(vdev, pos, size, errp);
        break;
    default:
        ret = pci_add_capability(pdev, cap_id, pos, size, errp) >= 0;
        break;
    }

    if (!ret) {
        error_prepend(errp,
                      "failed to add PCI capability 0x%x[0x%x]@0x%x: ",
                      cap_id, size, pos);
    }

    return ret;
}

static int vfio_setup_rebar_ecap(VFIOPCIDevice *vdev, uint16_t pos)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint32_t ctrl;
    int i, nbar;

    ctrl = pci_get_long(pdev->config + pos + PCI_REBAR_CTRL);
    nbar = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >> PCI_REBAR_CTRL_NBAR_SHIFT;

    for (i = 0; i < nbar; i++) {
        uint32_t cap;
        int size;

        ctrl = pci_get_long(pdev->config + pos + PCI_REBAR_CTRL + (i * 8));
        size = (ctrl & PCI_REBAR_CTRL_BAR_SIZE) >> PCI_REBAR_CTRL_BAR_SHIFT;

        /* The cap register reports sizes 1MB to 128TB, with 4 reserved bits */
        cap = size <= 27 ? 1U << (size + 4) : 0;

        /*
         * The PCIe spec (v6.0.1, 7.8.6) requires HW to support at least one
         * size in the range 1MB to 512GB.  We intend to mask all sizes except
         * the one currently enabled in the size field, therefore if it's
         * outside the range, hide the whole capability as this virtualization
         * trick won't work.  If >512GB resizable BARs start to appear, we
         * might need an opt-in or reservation scheme in the kernel.
         */
        if (!(cap & PCI_REBAR_CAP_SIZES)) {
            return -EINVAL;
        }

        /* Hide all sizes reported in the ctrl reg per above requirement. */
        ctrl &= (PCI_REBAR_CTRL_BAR_SIZE |
                 PCI_REBAR_CTRL_NBAR_MASK |
                 PCI_REBAR_CTRL_BAR_IDX);

        /*
         * The BAR size field is RW, however we've mangled the capability
         * register such that we only report a single size, ie. the current
         * BAR size.  A write of an unsupported value is undefined, therefore
         * the register field is essentially RO.
         */
        vfio_add_emulated_long(vdev, pos + PCI_REBAR_CAP + (i * 8), cap, ~0);
        vfio_add_emulated_long(vdev, pos + PCI_REBAR_CTRL + (i * 8), ctrl, ~0);
    }

    return 0;
}

/*
 * Try to retrieve PASID capability information via IOMMUFD APIs and,
 * if supported, synthesize a PASID PCIe extended capability for the
 * VFIO device.
 *
 * Use user-specified PASID capability offset if provided, otherwise
 * place it at the end of the PCIe extended configuration space.
 */
static bool vfio_pci_synthesize_pasid_cap(VFIOPCIDevice *vdev, Error **errp)
{
    HostIOMMUDevice *hiod = vdev->vbasedev.hiod;
    HostIOMMUDeviceClass *hiodc;
    PasidInfo pasid_info;
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint16_t pasid_offset;

    if (!hiod) {
        return true;
    }

    hiodc = HOST_IOMMU_DEVICE_GET_CLASS(hiod);
    if (!hiodc || !hiodc->get_pasid_info ||
        !hiodc->get_pasid_info(hiod, &pasid_info) ||
        !(pci_device_get_viommu_flags(pdev) & VIOMMU_FLAG_PASID_SUPPORTED)) {
        return true;
    }

    /* Use user-specified offset if set, otherwise place PASID at the end. */
    if (vdev->vpasid_cap_offset) {
        pasid_offset = vdev->vpasid_cap_offset;
    } else {
        pasid_offset = PCIE_CONFIG_SPACE_SIZE - PCI_EXT_CAP_PASID_SIZEOF;
    }

    if (!pcie_insert_capability(pdev, PCI_EXT_CAP_ID_PASID, PCI_PASID_VER,
                                pasid_offset, PCI_EXT_CAP_PASID_SIZEOF)) {
        error_setg(errp, "vfio: Placing PASID capability at offset 0x%x failed",
                   pasid_offset);
        return false;
    }
    trace_vfio_pci_synthesize_pasid_cap(vdev->vbasedev.name, pasid_offset);

    pcie_pasid_common_init(pdev, pasid_offset, pasid_info.max_pasid_log2,
                           pasid_info.exec_perm, pasid_info.priv_mod);

    /* PASID capability is fully emulated by QEMU */
    memset(vdev->emulated_config_bits + pdev->exp.pasid_cap, 0xff,
           PCI_EXT_CAP_PASID_SIZEOF);
    return true;
}

/*
 * Determine whether ATS capability should be advertised for @vdev, based on
 * whether it was enabled on the command line and whether it is supported
 * according to the kernel.
 *
 * Store whether ATS capability should be advertised in @ats_needed.
 *
 * Returns false only when ats=on is explicitly requested but the kernel
 * reports it is not supported. Returns true in all other cases.
 */
static bool vfio_pci_ats_requested_and_supported(VFIOPCIDevice *vdev,
                                                 bool *ats_needed, Error **errp)
{
    HostIOMMUDevice *hiod = vdev->vbasedev.hiod;
    HostIOMMUDeviceClass *hiodc;
    bool ats_supported;
    *ats_needed = false;

    if (vdev->ats == ON_OFF_AUTO_OFF) {
        return true;
    }

    *ats_needed = true;
    if (!hiod) {
        return true;
    }
    hiodc = HOST_IOMMU_DEVICE_GET_CLASS(hiod);
    if (!hiodc || !hiodc->support_ats) {
        return true;
    }

    ats_supported = hiodc->support_ats(hiod);
    if (vdev->ats == ON_OFF_AUTO_ON && !ats_supported) {
        error_setg(errp, "vfio-pci: ATS requested but not supported by kernel");
        *ats_needed = false;
        return false;
    }

    *ats_needed = ats_supported;
    return true;
}

static void vfio_add_ext_cap(VFIOPCIDevice *vdev, bool ats_needed)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    bool pasid_cap_added = false;
    bool ats_cap_present = false;
    Error *err = NULL;
    uint32_t header;
    uint16_t cap_id, next, size;
    uint8_t cap_ver;
    uint8_t *config;

    /* Only add extended caps if we have them and the guest can see them */
    if (!pci_is_express(pdev) || !pci_bus_is_express(pci_get_bus(pdev)) ||
        !pci_get_long(pdev->config + PCI_CONFIG_SPACE_SIZE)) {
        return;
    }

    /*
     * pcie_add_capability always inserts the new capability at the tail
     * of the chain.  Therefore to end up with a chain that matches the
     * physical device, we cache the config space to avoid overwriting
     * the original config space when we parse the extended capabilities.
     */
    config = g_memdup(pdev->config, vdev->config_size);

    /*
     * Extended capabilities are chained with each pointing to the next, so we
     * can drop anything other than the head of the chain simply by modifying
     * the previous next pointer.  Seed the head of the chain here such that
     * we can simply skip any capabilities we want to drop below, regardless
     * of their position in the chain.  If this stub capability still exists
     * after we add the capabilities we want to expose, update the capability
     * ID to zero.  Note that we cannot seed with the capability header being
     * zero as this conflicts with definition of an absent capability chain
     * and prevents capabilities beyond the head of the list from being added.
     * By replacing the dummy capability ID with zero after walking the device
     * chain, we also transparently mark extended capabilities as absent if
     * no capabilities were added.  Note that the PCIe spec defines an absence
     * of extended capabilities to be determined by a value of zero for the
     * capability ID, version, AND next pointer.  A non-zero next pointer
     * should be sufficient to indicate additional capabilities are present,
     * which will occur if we call pcie_add_capability() below.  The entire
     * first dword is emulated to support this.
     *
     * NB. The kernel side does similar masking, so be prepared that our
     * view of the device may also contain a capability ID zero in the head
     * of the chain.  Skip it for the same reason that we cannot seed the
     * chain with a zero capability.
     */
    pci_set_long(pdev->config + PCI_CONFIG_SPACE_SIZE,
                 PCI_EXT_CAP(0xFFFF, 0, 0));
    pci_set_long(pdev->wmask + PCI_CONFIG_SPACE_SIZE, 0);
    pci_set_long(vdev->emulated_config_bits + PCI_CONFIG_SPACE_SIZE, ~0);

    for (next = PCI_CONFIG_SPACE_SIZE; next;
         next = PCI_EXT_CAP_NEXT(pci_get_long(config + next))) {
        header = pci_get_long(config + next);
        cap_id = PCI_EXT_CAP_ID(header);
        cap_ver = PCI_EXT_CAP_VER(header);

        /*
         * If it becomes important to configure extended capabilities to their
         * actual size, use this as the default when it's something we don't
         * recognize. Since QEMU doesn't actually handle many of the config
         * accesses, exact size doesn't seem worthwhile.
         */
        size = vfio_ext_cap_max_size(config, next);

        /* Use emulated next pointer to allow dropping extended caps */
        pci_long_test_and_set_mask(vdev->emulated_config_bits + next,
                                   PCI_EXT_CAP_NEXT_MASK);

        switch (cap_id) {
        case 0: /* kernel masked capability */
        case PCI_EXT_CAP_ID_SRIOV: /* Read-only VF BARs confuse OVMF */
        case PCI_EXT_CAP_ID_ARI: /* XXX Needs next function virtualization */
            trace_vfio_add_ext_cap_dropped(vdev->vbasedev.name, cap_id, next);
            break;
        case PCI_EXT_CAP_ID_REBAR:
            if (!vfio_setup_rebar_ecap(vdev, next)) {
                pcie_add_capability(pdev, cap_id, cap_ver, next, size);
            }
            break;
        /*
         * VFIO kernel does not expose the PASID CAP today. We may synthesize
         * one later through IOMMUFD APIs. If VFIO ever starts exposing it,
         * record its presence here so we do not create a duplicate CAP.
         */
        case PCI_EXT_CAP_ID_PASID:
            pasid_cap_added = true;
            pcie_add_capability(pdev, cap_id, cap_ver, next, size);
            break;
        case PCI_EXT_CAP_ID_ATS:
            ats_cap_present = true;
            /*
             * If ATS is requested and supported according to the kernel, add
             * the ATS capability. If not supported according to the kernel or
             * disabled on the qemu command line, omit the ATS cap.
             */
            if (ats_needed) {
                pcie_add_capability(pdev, cap_id, cap_ver, next, size);
            }
            break;
        default:
            pcie_add_capability(pdev, cap_id, cap_ver, next, size);
        }

    }

    if (!pasid_cap_added && !vfio_pci_synthesize_pasid_cap(vdev, &err)) {
        error_report_err(err);
    }

    if (vdev->ats == ON_OFF_AUTO_ON && !ats_cap_present) {
        warn_report("vfio-pci: ats=on requested, but host device has no "
                    "ATS extended capability");
    }

    if (vdev->ats == ON_OFF_AUTO_AUTO && ats_cap_present && !ats_needed) {
        warn_report("vfio-pci: host kernel reports ATS unsupported; "
                    "ATS capability will be masked");
    }

    /* Cleanup chain head ID if necessary */
    if (pci_get_word(pdev->config + PCI_CONFIG_SPACE_SIZE) == 0xFFFF) {
        pci_set_word(pdev->config + PCI_CONFIG_SPACE_SIZE, 0);
    }

    g_free(config);
}

bool vfio_pci_add_capabilities(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    bool ats_needed = false;

    if (!(pdev->config[PCI_STATUS] & PCI_STATUS_CAP_LIST) ||
        !pdev->config[PCI_CAPABILITY_LIST]) {
        return true; /* Nothing to add */
    }

    if (!vfio_add_std_cap(vdev, pdev->config[PCI_CAPABILITY_LIST], errp)) {
        return false;
    }

    if (!vfio_pci_ats_requested_and_supported(vdev, &ats_needed, errp)) {
        return false;
    }

    vfio_add_ext_cap(vdev, ats_needed);
    return true;
}

void vfio_pci_pre_reset(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    uint32_t val;
    uint16_t cmd;

    vfio_pci_irq_disable(vdev);

    /*
     * Stop any ongoing DMA by disconnecting I/O, MMIO, and bus master.
     * Also put INTx Disable in known state.
     */
    val = vfio_pci_read_config(pdev, PCI_COMMAND, 2);
    if (val != (uint32_t)-1) {
        cmd = val;
        cmd &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
                 PCI_COMMAND_INTX_DISABLE);
        vfio_pci_write_config(pdev, PCI_COMMAND, cmd, 2);
    }

    /* Make sure the device is in D0 */
    if (pdev->pm_cap) {
        uint16_t pmcsr;
        uint8_t state;

        val = vfio_pci_read_config(pdev, pdev->pm_cap + PCI_PM_CTRL, 2);
        if (val == (uint32_t)-1) {
            return;
        }
        pmcsr = val;
        state = pmcsr & PCI_PM_CTRL_STATE_MASK;
        if (state) {
            pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
            vfio_pci_write_config(pdev, pdev->pm_cap + PCI_PM_CTRL, pmcsr, 2);
            /* vfio handles the necessary delay here */
            val = vfio_pci_read_config(pdev, pdev->pm_cap + PCI_PM_CTRL, 2);
            if (val == (uint32_t)-1) {
                return;
            }
            pmcsr = val;
            state = pmcsr & PCI_PM_CTRL_STATE_MASK;
            if (state) {
                error_report("vfio: Unable to power on device, stuck in D%d",
                             state);
            }
        }
    }
}

void vfio_pci_post_reset(VFIOPCIDevice *vdev)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    Error *err = NULL;
    int ret, nr;

    if (!vfio_pci_intx_enable(vdev, &err)) {
        error_reportf_err(err, VFIO_MSG_PREFIX, vdev->vbasedev.name);
    }

    for (nr = 0; nr < PCI_NUM_REGIONS - 1; ++nr) {
        off_t addr = PCI_BASE_ADDRESS_0 + (4 * nr);
        uint32_t val = 0;
        uint32_t len = sizeof(val);

        ret = vfio_pci_config_space_write(vdev, addr, len, &val);
        if (ret != len) {
            error_report("%s(%s) reset bar %d failed: %s", __func__,
                         vbasedev->name, nr, strwriteerror(ret));
        }
    }

    vfio_quirk_reset(vdev);
}

bool vfio_pci_host_match(PCIHostDeviceAddress *addr, const char *name)
{
    char tmp[36];

    sprintf(tmp, "%04x:%02x:%02x.%1x", addr->domain,
            addr->bus, addr->slot, addr->function);

    return (strcmp(tmp, name) == 0);
}

int vfio_pci_get_pci_hot_reset_info(VFIOPCIDevice *vdev,
                                    struct vfio_pci_hot_reset_info **info_p)
{
    struct vfio_pci_hot_reset_info *info;
    int ret, count;

    assert(info_p && !*info_p);

    info = g_malloc0(sizeof(*info));
    info->argsz = sizeof(*info);

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, info);
    if (ret && errno != ENOSPC) {
        ret = -errno;
        g_free(info);
        if (!vdev->has_pm_reset) {
            error_report("vfio: Cannot reset device %s, "
                         "no available reset mechanism.", vdev->vbasedev.name);
        }
        return ret;
    }

    count = info->count;
    info = g_realloc(info, sizeof(*info) + (count * sizeof(info->devices[0])));
    info->argsz = sizeof(*info) + (count * sizeof(info->devices[0]));

    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, info);
    if (ret) {
        ret = -errno;
        g_free(info);
        error_report("vfio: hot reset info failed: %m");
        return ret;
    }

    *info_p = info;
    return 0;
}

static int vfio_pci_hot_reset(VFIOPCIDevice *vdev, bool single)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    const VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(vbasedev->bcontainer);

    return vioc->pci_hot_reset(vbasedev, single);
}

/*
 * We want to differentiate hot reset of multiple in-use devices vs hot reset
 * of a single in-use device.  VFIO_DEVICE_RESET will already handle the case
 * of doing hot resets when there is only a single device per bus.  The in-use
 * here refers to how many VFIODevices are affected.  A hot reset that affects
 * multiple devices, but only a single in-use device, means that we can call
 * it from our bus ->reset() callback since the extent is effectively a single
 * device.  This allows us to make use of it in the hotplug path.  When there
 * are multiple in-use devices, we can only trigger the hot reset during a
 * system reset and thus from our reset handler.  We separate _one vs _multi
 * here so that we don't overlap and do a double reset on the system reset
 * path where both our reset handler and ->reset() callback are used.  Calling
 * _one() will only do a hot reset for the one in-use devices case, calling
 * _multi() will do nothing if a _one() would have been sufficient.
 */
static int vfio_pci_hot_reset_one(VFIOPCIDevice *vdev)
{
    return vfio_pci_hot_reset(vdev, true);
}

static int vfio_pci_hot_reset_multi(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    return vfio_pci_hot_reset(vdev, false);
}

static void vfio_pci_compute_needs_reset(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    if (!vbasedev->reset_works || (!vdev->has_flr && vdev->has_pm_reset)) {
        vbasedev->needs_reset = true;
    }
}

static Object *vfio_pci_get_object(VFIODevice *vbasedev)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);

    return OBJECT(vdev);
}

static bool vfio_msix_present(void *opaque, int version_id)
{
    PCIDevice *pdev = opaque;

    return msix_present(pdev);
}

static bool vfio_display_migration_needed(void *opaque)
{
    VFIOPCIDevice *vdev = opaque;

    /*
     * We need to migrate the VFIODisplay object if ramfb *migration* was
     * explicitly requested (in which case we enforced both ramfb=on and
     * display=on), or ramfb migration was left at the default "auto"
     * setting, and *ramfb* was explicitly requested (in which case we
     * enforced display=on).
     */
    return vdev->ramfb_migrate == ON_OFF_AUTO_ON ||
        (vdev->ramfb_migrate == ON_OFF_AUTO_AUTO && vdev->enable_ramfb);
}

static const VMStateDescription vmstate_vfio_display = {
    .name = "VFIOPCIDevice/VFIODisplay",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vfio_display_migration_needed,
    .fields = (const VMStateField[]){
        VMSTATE_STRUCT_POINTER(dpy, VFIOPCIDevice, vfio_display_vmstate,
                               VFIODisplay),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vfio_pci_config = {
    .name = "VFIOPCIDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, VFIOPCIDevice),
        VMSTATE_MSIX_TEST(parent_obj, VFIOPCIDevice, vfio_msix_present),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_vfio_display,
        NULL
    }
};

static int vfio_pci_save_config(VFIODevice *vbasedev, QEMUFile *f, Error **errp)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);

    return vmstate_save_state(f, &vmstate_vfio_pci_config, vdev, NULL,
                              errp);
}

static int vfio_pci_load_config(VFIODevice *vbasedev, QEMUFile *f)
{
    VFIOPCIDevice *vdev = container_of(vbasedev, VFIOPCIDevice, vbasedev);
    PCIDevice *pdev = PCI_DEVICE(vdev);
    pcibus_t old_addr[PCI_NUM_REGIONS - 1];
    int bar, ret;
    Error *local_err = NULL;

    for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
        old_addr[bar] = pdev->io_regions[bar].addr;
    }

    ret = vmstate_load_state(f, &vmstate_vfio_pci_config, vdev, 1,
                             &local_err);
    if (ret) {
        error_report_err(local_err);
        return ret;
    }

    vfio_pci_write_config(pdev, PCI_COMMAND,
                          pci_get_word(pdev->config + PCI_COMMAND), 2);

    for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
        /*
         * The address may not be changed in some scenarios
         * (e.g. the VF driver isn't loaded in VM).
         */
        if (old_addr[bar] != pdev->io_regions[bar].addr &&
            vdev->bars[bar].region.size > 0 &&
            vdev->bars[bar].region.size < qemu_real_host_page_size()) {
            vfio_sub_page_bar_update_mapping(pdev, bar);
        }
    }

    if (msi_enabled(pdev)) {
        vfio_pci_msi_enable(vdev);
    } else if (msix_enabled(pdev)) {
        vfio_pci_msix_enable(vdev);
    }

    return ret;
}

/* Transform from VFIODevice to VFIOPCIDevice. Return NULL if fails. */
VFIOPCIDevice *vfio_pci_from_vfio_device(VFIODevice *vbasedev)
{
    if (vbasedev && vbasedev->type == VFIO_DEVICE_TYPE_PCI) {
        return container_of(vbasedev, VFIOPCIDevice, vbasedev);
    }
    return NULL;
}

void vfio_sub_page_bar_update_mappings(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    int page_size = qemu_real_host_page_size();
    int bar;

    for (bar = 0; bar < PCI_ROM_SLOT; bar++) {
        PCIIORegion *r = &pdev->io_regions[bar];
        if (r->addr != PCI_BAR_UNMAPPED && r->size > 0 && r->size < page_size) {
            vfio_sub_page_bar_update_mapping(pdev, bar);
        }
    }
}

static VFIODeviceOps vfio_pci_ops = {
    .vfio_compute_needs_reset = vfio_pci_compute_needs_reset,
    .vfio_hot_reset_multi = vfio_pci_hot_reset_multi,
    .vfio_eoi = vfio_pci_intx_eoi,
    .vfio_get_object = vfio_pci_get_object,
    .vfio_save_config = vfio_pci_save_config,
    .vfio_load_config = vfio_pci_load_config,
};

bool vfio_populate_vga(VFIOPCIDevice *vdev, Error **errp)
{
    VFIODevice *vbasedev = &vdev->vbasedev;
    struct vfio_region_info *reg_info = NULL;
    int ret;

    ret = vfio_device_get_region_info(vbasedev, VFIO_PCI_VGA_REGION_INDEX, &reg_info);
    if (ret) {
        error_setg_errno(errp, -ret,
                         "failed getting region info for VGA region index %d",
                         VFIO_PCI_VGA_REGION_INDEX);
        return false;
    }

    if (!(reg_info->flags & VFIO_REGION_INFO_FLAG_READ) ||
        !(reg_info->flags & VFIO_REGION_INFO_FLAG_WRITE) ||
        reg_info->size < 0xbffff + 1) {
        error_setg(errp, "unexpected VGA info, flags 0x%lx, size 0x%lx",
                   (unsigned long)reg_info->flags,
                   (unsigned long)reg_info->size);
        return false;
    }

    vdev->vga = g_new0(VFIOVGA, 1);

    vdev->vga->fd_offset = reg_info->offset;
    vdev->vga->fd = vdev->vbasedev.fd;

    vdev->vga->region[QEMU_PCI_VGA_MEM].offset = QEMU_PCI_VGA_MEM_BASE;
    vdev->vga->region[QEMU_PCI_VGA_MEM].nr = QEMU_PCI_VGA_MEM;
    QLIST_INIT(&vdev->vga->region[QEMU_PCI_VGA_MEM].quirks);

    memory_region_init_io(&vdev->vga->region[QEMU_PCI_VGA_MEM].mem,
                          OBJECT(vdev), &vfio_vga_ops,
                          &vdev->vga->region[QEMU_PCI_VGA_MEM],
                          "vfio-vga-mmio@0xa0000",
                          QEMU_PCI_VGA_MEM_SIZE);

    vdev->vga->region[QEMU_PCI_VGA_IO_LO].offset = QEMU_PCI_VGA_IO_LO_BASE;
    vdev->vga->region[QEMU_PCI_VGA_IO_LO].nr = QEMU_PCI_VGA_IO_LO;
    QLIST_INIT(&vdev->vga->region[QEMU_PCI_VGA_IO_LO].quirks);

    memory_region_init_io(&vdev->vga->region[QEMU_PCI_VGA_IO_LO].mem,
                          OBJECT(vdev), &vfio_vga_ops,
                          &vdev->vga->region[QEMU_PCI_VGA_IO_LO],
                          "vfio-vga-io@0x3b0",
                          QEMU_PCI_VGA_IO_LO_SIZE);

    vdev->vga->region[QEMU_PCI_VGA_IO_HI].offset = QEMU_PCI_VGA_IO_HI_BASE;
    vdev->vga->region[QEMU_PCI_VGA_IO_HI].nr = QEMU_PCI_VGA_IO_HI;
    QLIST_INIT(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].quirks);

    memory_region_init_io(&vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem,
                          OBJECT(vdev), &vfio_vga_ops,
                          &vdev->vga->region[QEMU_PCI_VGA_IO_HI],
                          "vfio-vga-io@0x3c0",
                          QEMU_PCI_VGA_IO_HI_SIZE);

    return true;
}

bool vfio_pci_populate_device(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    struct vfio_region_info *reg_info = NULL;
    struct vfio_irq_info irq_info;
    int i, ret = -1;

    /* Sanity check device */
    if (!(vbasedev->flags & VFIO_DEVICE_FLAGS_PCI)) {
        error_setg(errp, "this isn't a PCI device");
        return false;
    }

    if (vbasedev->num_initial_regions < VFIO_PCI_CONFIG_REGION_INDEX + 1) {
        error_setg(errp, "unexpected number of io regions %u",
                   vbasedev->num_initial_regions);
        return false;
    }

    if (vbasedev->num_irqs < VFIO_PCI_MSIX_IRQ_INDEX + 1) {
        error_setg(errp, "unexpected number of irqs %u", vbasedev->num_irqs);
        return false;
    }

    for (i = VFIO_PCI_BAR0_REGION_INDEX; i < VFIO_PCI_ROM_REGION_INDEX; i++) {
        char *name = g_strdup_printf("%s BAR %d", vbasedev->name, i);

        ret = vfio_region_setup(OBJECT(vdev), vbasedev,
                                &vdev->bars[i].region, i, name, errp);
        g_free(name);

        if (ret) {
            return false;
        }

        QLIST_INIT(&vdev->bars[i].quirks);
    }

    ret = vfio_device_get_region_info(vbasedev,
                                      VFIO_PCI_CONFIG_REGION_INDEX, &reg_info);
    if (ret) {
        error_setg_errno(errp, -ret, "failed to get config info");
        return false;
    }

    trace_vfio_pci_populate_device_config(vdev->vbasedev.name,
                                      (unsigned long)reg_info->size,
                                      (unsigned long)reg_info->offset,
                                      (unsigned long)reg_info->flags);

    vdev->config_size = reg_info->size;
    if (vdev->config_size == PCI_CONFIG_SPACE_SIZE) {
        pdev->cap_present &= ~QEMU_PCI_CAP_EXPRESS;
    }
    vdev->config_offset = reg_info->offset;

    if (vdev->features & VFIO_FEATURE_ENABLE_VGA) {
        if (!vfio_populate_vga(vdev, errp)) {
            error_append_hint(errp, "device does not support "
                              "requested feature x-vga\n");
            return false;
        }
    }

    ret = vfio_device_get_irq_info(vbasedev, VFIO_PCI_ERR_IRQ_INDEX, &irq_info);
    if (ret) {
        /* This can fail for an old kernel or legacy PCI dev */
        trace_vfio_pci_populate_device_get_irq_info_failure(strerror(-ret));
    } else if (irq_info.count == 1) {
        vdev->pci_aer = true;
    } else {
        warn_report(VFIO_MSG_PREFIX
                    "Could not enable error recovery for the device",
                    vbasedev->name);
    }

    return true;
}

void vfio_pci_put_device(VFIOPCIDevice *vdev)
{
    vfio_display_finalize(vdev);
    vfio_bars_finalize(vdev);
    vfio_cpr_pci_unregister_device(vdev);
    g_free(vdev->emulated_config_bits);
    g_free(vdev->rom);
    /*
     * XXX Leaking igd_opregion is not an oversight, we can't remove the
     * fw_cfg entry therefore leaking this allocation seems like the safest
     * option.
     *
     * g_free(vdev->igd_opregion);
     */

    vfio_device_detach(&vdev->vbasedev);

    vfio_device_free_name(&vdev->vbasedev);
    g_free(vdev->msix);
}

void vfio_pci_config_register_vga(VFIOPCIDevice *vdev)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    assert(vdev->vga != NULL);

    pci_register_vga(pdev, &vdev->vga->region[QEMU_PCI_VGA_MEM].mem,
                     &vdev->vga->region[QEMU_PCI_VGA_IO_LO].mem,
                     &vdev->vga->region[QEMU_PCI_VGA_IO_HI].mem);
}

bool vfio_pci_config_setup(VFIOPCIDevice *vdev, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(vdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    uint32_t config_space_size;
    int ret;

    config_space_size = MIN(pci_config_size(pdev), vdev->config_size);

    /* Get a copy of config space */
    ret = vfio_pci_config_space_read(vdev, 0, config_space_size,
                                     pdev->config);
    if (ret < (int)config_space_size) {
        ret = ret < 0 ? -ret : EFAULT;
        error_setg_errno(errp, ret, "failed to read device config space");
        return false;
    }

    /* vfio emulates a lot for us, but some bits need extra love */
    vdev->emulated_config_bits = g_malloc0(vdev->config_size);

    /* QEMU can choose to expose the ROM or not */
    memset(vdev->emulated_config_bits + PCI_ROM_ADDRESS, 0xff, 4);
    /* QEMU can also add or extend BARs */
    memset(vdev->emulated_config_bits + PCI_BASE_ADDRESS_0, 0xff, 6 * 4);

    /*
     * The PCI spec reserves vendor ID 0xffff as an invalid value.  The
     * device ID is managed by the vendor and need only be a 16-bit value.
     * Allow any 16-bit value for subsystem so they can be hidden or changed.
     */
    if (vdev->vendor_id != PCI_ANY_ID) {
        if (vdev->vendor_id >= 0xffff) {
            error_setg(errp, "invalid PCI vendor ID provided");
            return false;
        }
        vfio_add_emulated_word(vdev, PCI_VENDOR_ID, vdev->vendor_id, ~0);
        trace_vfio_pci_emulated_vendor_id(vbasedev->name, vdev->vendor_id);
    } else {
        vdev->vendor_id = pci_get_word(pdev->config + PCI_VENDOR_ID);
    }

    if (vdev->device_id != PCI_ANY_ID) {
        if (vdev->device_id > 0xffff) {
            error_setg(errp, "invalid PCI device ID provided");
            return false;
        }
        vfio_add_emulated_word(vdev, PCI_DEVICE_ID, vdev->device_id, ~0);
        trace_vfio_pci_emulated_device_id(vbasedev->name, vdev->device_id);
    } else {
        vdev->device_id = pci_get_word(pdev->config + PCI_DEVICE_ID);
    }

    if (vdev->sub_vendor_id != PCI_ANY_ID) {
        if (vdev->sub_vendor_id > 0xffff) {
            error_setg(errp, "invalid PCI subsystem vendor ID provided");
            return false;
        }
        vfio_add_emulated_word(vdev, PCI_SUBSYSTEM_VENDOR_ID,
                               vdev->sub_vendor_id, ~0);
        trace_vfio_pci_emulated_sub_vendor_id(vbasedev->name,
                                              vdev->sub_vendor_id);
    }

    if (vdev->sub_device_id != PCI_ANY_ID) {
        if (vdev->sub_device_id > 0xffff) {
            error_setg(errp, "invalid PCI subsystem device ID provided");
            return false;
        }
        vfio_add_emulated_word(vdev, PCI_SUBSYSTEM_ID, vdev->sub_device_id, ~0);
        trace_vfio_pci_emulated_sub_device_id(vbasedev->name,
                                              vdev->sub_device_id);
    }

    /*
     * Class code is a 24-bit value at config space 0x09. Allow overriding it
     * with any 24-bit value.
     */
    if (vdev->class_code != PCI_ANY_ID) {
        if (vdev->class_code > 0xffffff) {
            error_setg(errp, "invalid PCI class code provided");
            return false;
        }
        /* Higher 24 bits of PCI_CLASS_REVISION are class code */
        vfio_add_emulated_long(vdev, PCI_CLASS_REVISION,
                               vdev->class_code << 8, ~0xff);
        trace_vfio_pci_emulated_class_code(vbasedev->name, vdev->class_code);
    } else {
        vdev->class_code = pci_get_long(pdev->config + PCI_CLASS_REVISION) >> 8;
    }

    /* QEMU can change multi-function devices to single function, or reverse */
    vdev->emulated_config_bits[PCI_HEADER_TYPE] =
                                              PCI_HEADER_TYPE_MULTI_FUNCTION;

    /* Restore or clear multifunction, this is always controlled by QEMU */
    if (pdev->cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        pdev->config[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;
    } else {
        pdev->config[PCI_HEADER_TYPE] &= ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    }

    /*
     * Clear host resource mapping info.  If we choose not to register a
     * BAR, such as might be the case with the option ROM, we can get
     * confusing, unwritable, residual addresses from the host here.
     */
    memset(&pdev->config[PCI_BASE_ADDRESS_0], 0, 24);
    memset(&pdev->config[PCI_ROM_ADDRESS], 0, 4);

    vfio_pci_size_rom(vdev);

    vfio_bars_prepare(vdev);

    if (!vfio_pci_msix_early_setup(vdev, errp)) {
        return false;
    }

    vfio_bars_register(vdev);

    if (vdev->vga && vfio_is_vga(vdev)) {
        vfio_pci_config_register_vga(vdev);
    }

    return true;
}

static void vfio_pci_realize(PCIDevice *pdev, Error **errp)
{
    ERRP_GUARD();
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;
    int i;
    char uuid[UUID_STR_LEN];
    g_autofree char *name = NULL;

    if (vbasedev->fd < 0 && !vbasedev->sysfsdev) {
        if (!(~vdev->host.domain || ~vdev->host.bus ||
              ~vdev->host.slot || ~vdev->host.function)) {
            error_setg(errp, "No provided host device");
            error_append_hint(errp, "Use -device vfio-pci,host=DDDD:BB:DD.F "
                              "or -device vfio-pci,fd=DEVICE_FD "
                              "or -device vfio-pci,sysfsdev=PATH_TO_DEVICE\n");
            return;
        }
        vbasedev->sysfsdev =
            g_strdup_printf("/sys/bus/pci/devices/%04x:%02x:%02x.%01x",
                            vdev->host.domain, vdev->host.bus,
                            vdev->host.slot, vdev->host.function);
    }

    if (!vfio_device_get_name(vbasedev, errp)) {
        return;
    }

    /*
     * Mediated devices *might* operate compatibly with discarding of RAM, but
     * we cannot know for certain, it depends on whether the mdev vendor driver
     * stays in sync with the active working set of the guest driver.  Prevent
     * the x-balloon-allowed option unless this is minimally an mdev device.
     */
    vbasedev->mdev = vfio_device_is_mdev(vbasedev);

    trace_vfio_mdev(vbasedev->name, vbasedev->mdev);

    if (vbasedev->ram_block_discard_allowed && !vbasedev->mdev) {
        error_setg(errp, "x-balloon-allowed only potentially compatible "
                   "with mdev devices");
        goto error;
    }

    if (!qemu_uuid_is_null(&vdev->vf_token)) {
        qemu_uuid_unparse(&vdev->vf_token, uuid);
        name = g_strdup_printf("%s vf_token=%s", vbasedev->name, uuid);
    } else {
        name = g_strdup(vbasedev->name);
    }

    if (!vfio_device_attach(name, vbasedev,
                            pci_device_iommu_address_space(pdev), errp)) {
        goto error;
    }

    if (!vfio_pci_populate_device(vdev, errp)) {
        goto error;
    }

    if (!vfio_pci_config_setup(vdev, errp)) {
        goto error;
    }

    if (!vbasedev->mdev &&
        !pci_device_set_iommu_device(pdev, vbasedev->hiod, errp)) {
        error_prepend(errp, "Failed to set vIOMMU: ");
        goto out_teardown;
    }

    if (!vfio_pci_add_capabilities(vdev, errp)) {
        goto out_unset_idev;
    }

    if (!vfio_config_quirk_setup(vdev, errp)) {
        goto out_unset_idev;
    }

    if (vdev->vga) {
        vfio_vga_quirk_setup(vdev);
    }

    for (i = 0; i < PCI_ROM_SLOT; i++) {
        vfio_bar_quirk_setup(vdev, i);
    }

    if (!vfio_pci_irq_setup(vdev, errp)) {
        goto out_unset_idev;
    }

    if (vdev->display != ON_OFF_AUTO_OFF) {
        if (!vfio_display_probe(vdev, errp)) {
            goto out_deregister;
        }
    }
    if (vdev->enable_ramfb && vdev->dpy == NULL) {
        error_setg(errp, "ramfb=on requires display=on");
        goto out_deregister;
    }
    if (vdev->display_xres || vdev->display_yres) {
        if (vdev->dpy == NULL) {
            error_setg(errp, "xres and yres properties require display=on");
            goto out_deregister;
        }
        if (vdev->dpy->edid_regs == NULL) {
            error_setg(errp, "xres and yres properties need edid support");
            goto out_deregister;
        }
    }

    if (vdev->ramfb_migrate == ON_OFF_AUTO_ON && !vdev->enable_ramfb) {
        warn_report("x-ramfb-migrate=on but ramfb=off. "
                    "Forcing x-ramfb-migrate to off.");
        vdev->ramfb_migrate = ON_OFF_AUTO_OFF;
    }
    if (vbasedev->enable_migration == ON_OFF_AUTO_OFF) {
        if (vdev->ramfb_migrate == ON_OFF_AUTO_AUTO) {
            vdev->ramfb_migrate = ON_OFF_AUTO_OFF;
        } else if (vdev->ramfb_migrate == ON_OFF_AUTO_ON) {
            error_setg(errp, "x-ramfb-migrate requires enable-migration");
            goto out_deregister;
        }
    }

    if (!pdev->failover_pair_id) {
        if (!vfio_migration_realize(vbasedev, errp)) {
            goto out_deregister;
        }
    }

    vfio_pci_register_err_notifier(vdev);
    vfio_pci_register_req_notifier(vdev);
    vfio_setup_resetfn_quirk(vdev);
    vfio_cpr_pci_register_device(vdev);

    return;

out_deregister:
    vfio_display_exit(vdev);
    if (vdev->interrupt == VFIO_INT_INTx) {
        vfio_pci_intx_disable(vdev);
    }
    pci_device_set_intx_routing_notifier(pdev, NULL);
    if (vdev->irqchip_change_notifier.notify) {
        kvm_irqchip_remove_change_notifier(&vdev->irqchip_change_notifier);
    }
    if (vdev->intx.mmap_timer) {
        timer_free(vdev->intx.mmap_timer);
    }
out_unset_idev:
    if (!vbasedev->mdev) {
        pci_device_unset_iommu_device(pdev);
    }
out_teardown:
    vfio_pci_msi_teardown(vdev);
    vfio_pci_bars_exit(vdev);
error:
    error_prepend(errp, VFIO_MSG_PREFIX, vbasedev->name);
}

static void vfio_pci_finalize(Object *obj)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(obj);

    vfio_pci_put_device(vdev);
}

static void vfio_exitfn(PCIDevice *pdev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(pdev);
    VFIODevice *vbasedev = &vdev->vbasedev;

    vfio_display_exit(vdev);
    vfio_pci_unregister_req_notifier(vdev);
    vfio_pci_unregister_err_notifier(vdev);
    pci_device_set_intx_routing_notifier(pdev, NULL);
    if (vdev->irqchip_change_notifier.notify) {
        kvm_irqchip_remove_change_notifier(&vdev->irqchip_change_notifier);
    }
    vfio_pci_irq_disable(vdev);
    if (vdev->intx.mmap_timer) {
        timer_free(vdev->intx.mmap_timer);
    }
    vfio_pci_msi_teardown(vdev);
    vfio_pci_disable_rp_atomics(vdev);
    vfio_pci_bars_exit(vdev);
    vfio_migration_exit(vbasedev);
    if (!vbasedev->mdev) {
        pci_device_unset_iommu_device(pdev);
    }
}

static void vfio_pci_reset(DeviceState *dev)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(dev);

    /* Do not reset the device during qemu_system_reset prior to cpr load */
    if (cpr_is_incoming()) {
        return;
    }

    trace_vfio_pci_reset(vdev->vbasedev.name);

    vfio_pci_pre_reset(vdev);

    if (vdev->display != ON_OFF_AUTO_OFF) {
        vfio_display_reset(vdev);
    }

    if (vdev->resetfn && !vdev->resetfn(vdev)) {
        goto post_reset;
    }

    if (vdev->vbasedev.reset_works &&
        (vdev->has_flr || !vdev->has_pm_reset) &&
        !ioctl(vdev->vbasedev.fd, VFIO_DEVICE_RESET)) {
        trace_vfio_pci_reset_flr(vdev->vbasedev.name);
        goto post_reset;
    }

    /* See if we can do our own bus reset */
    if (!vfio_pci_hot_reset_one(vdev)) {
        goto post_reset;
    }

    /* If nothing else works and the device supports PM reset, use it */
    if (vdev->vbasedev.reset_works && vdev->has_pm_reset &&
        !ioctl(vdev->vbasedev.fd, VFIO_DEVICE_RESET)) {
        trace_vfio_pci_reset_pm(vdev->vbasedev.name);
        goto post_reset;
    }

post_reset:
    vfio_pci_post_reset(vdev);
}

static void vfio_pci_init(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(obj);
    VFIODevice *vbasedev = &vdev->vbasedev;

    device_add_bootindex_property(obj, &vdev->bootindex,
                                  "bootindex", NULL,
                                  &pci_dev->qdev);
    vdev->host.domain = ~0U;
    vdev->host.bus = ~0U;
    vdev->host.slot = ~0U;
    vdev->host.function = ~0U;

    vfio_device_init(vbasedev, VFIO_DEVICE_TYPE_PCI, &vfio_pci_ops,
                     DEVICE(vdev), false);

    vdev->nv_gpudirect_clique = 0xFF;

    /* QEMU_PCI_CAP_EXPRESS initialization does not depend on QEMU command
     * line, therefore, no need to wait to realize like other devices */
    pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;

    /*
     * A device that is resuming for cpr is already configured, so do not
     * reset it during qemu_system_reset prior to cpr load, else interrupts
     * may be lost.
     */
    pci_dev->cap_present |= QEMU_PCI_SKIP_RESET_ON_CPR;
}

static void vfio_pci_device_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    dc->desc = "VFIO PCI base device";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    pdc->exit = vfio_exitfn;
    pdc->config_read = vfio_pci_read_config;
    pdc->config_write = vfio_pci_write_config;
}

static const TypeInfo vfio_pci_device_info = {
    .name = TYPE_VFIO_PCI_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VFIOPCIDevice),
    .abstract = true,
    .class_init = vfio_pci_device_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};

static PropertyInfo vfio_pci_migration_multifd_transfer_prop;

static const Property vfio_pci_properties[] = {
    DEFINE_PROP_PCI_HOST_DEVADDR("host", VFIOPCIDevice, host),
    DEFINE_PROP_UUID_NODEFAULT("vf-token", VFIOPCIDevice, vf_token),
    DEFINE_PROP_STRING("sysfsdev", VFIOPCIDevice, vbasedev.sysfsdev),
    DEFINE_PROP_ON_OFF_AUTO("x-pre-copy-dirty-page-tracking", VFIOPCIDevice,
                            vbasedev.pre_copy_dirty_page_tracking,
                            ON_OFF_AUTO_ON),
    DEFINE_PROP_ON_OFF_AUTO("x-device-dirty-page-tracking", VFIOPCIDevice,
                            vbasedev.device_dirty_page_tracking,
                            ON_OFF_AUTO_ON),
    DEFINE_PROP_ON_OFF_AUTO("display", VFIOPCIDevice,
                            display, ON_OFF_AUTO_OFF),
    DEFINE_PROP_UINT32("xres", VFIOPCIDevice, display_xres, 0),
    DEFINE_PROP_UINT32("yres", VFIOPCIDevice, display_yres, 0),
    DEFINE_PROP_UINT32("x-intx-mmap-timeout-ms", VFIOPCIDevice,
                       intx.mmap_timeout, 1100),
    DEFINE_PROP_BIT("x-vga", VFIOPCIDevice, features,
                    VFIO_FEATURE_ENABLE_VGA_BIT, false),
    DEFINE_PROP_BIT("x-req", VFIOPCIDevice, features,
                    VFIO_FEATURE_ENABLE_REQ_BIT, true),
    DEFINE_PROP_BIT("x-igd-opregion", VFIOPCIDevice, features,
                    VFIO_FEATURE_ENABLE_IGD_OPREGION_BIT, true),
    DEFINE_PROP_BIT("x-igd-lpc", VFIOPCIDevice, features,
                    VFIO_FEATURE_ENABLE_IGD_LPC_BIT, false),
    DEFINE_PROP_ON_OFF_AUTO("x-igd-legacy-mode", VFIOPCIDevice,
                            igd_legacy_mode, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_ON_OFF_AUTO("enable-migration", VFIOPCIDevice,
                            vbasedev.enable_migration, ON_OFF_AUTO_AUTO),
    DEFINE_PROP("x-migration-multifd-transfer", VFIOPCIDevice,
                vbasedev.migration_multifd_transfer,
                vfio_pci_migration_multifd_transfer_prop, OnOffAuto,
                .set_default = true, .defval.i = ON_OFF_AUTO_AUTO),
    DEFINE_PROP_ON_OFF_AUTO("x-migration-load-config-after-iter", VFIOPCIDevice,
                            vbasedev.migration_load_config_after_iter,
                            ON_OFF_AUTO_AUTO),
    DEFINE_PROP_SIZE("x-migration-max-queued-buffers-size", VFIOPCIDevice,
                     vbasedev.migration_max_queued_buffers_size, UINT64_MAX),
    DEFINE_PROP_BOOL("migration-events", VFIOPCIDevice,
                     vbasedev.migration_events, false),
    DEFINE_PROP_BOOL("x-no-mmap", VFIOPCIDevice, vbasedev.no_mmap, false),
    DEFINE_PROP_BOOL("x-balloon-allowed", VFIOPCIDevice,
                     vbasedev.ram_block_discard_allowed, false),
    DEFINE_PROP_BOOL("x-no-kvm-intx", VFIOPCIDevice, no_kvm_intx, false),
    DEFINE_PROP_BOOL("x-no-kvm-msi", VFIOPCIDevice, no_kvm_msi, false),
    DEFINE_PROP_BOOL("x-no-kvm-msix", VFIOPCIDevice, no_kvm_msix, false),
    DEFINE_PROP_BOOL("x-no-geforce-quirks", VFIOPCIDevice,
                     no_geforce_quirks, false),
    DEFINE_PROP_BOOL("x-no-kvm-ioeventfd", VFIOPCIDevice, no_kvm_ioeventfd,
                     false),
    DEFINE_PROP_BOOL("x-no-vfio-ioeventfd", VFIOPCIDevice, no_vfio_ioeventfd,
                     false),
    DEFINE_PROP_UINT32("x-pci-vendor-id", VFIOPCIDevice, vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-device-id", VFIOPCIDevice, device_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-vendor-id", VFIOPCIDevice,
                       sub_vendor_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-sub-device-id", VFIOPCIDevice,
                       sub_device_id, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-pci-class-code", VFIOPCIDevice,
                       class_code, PCI_ANY_ID),
    DEFINE_PROP_UINT32("x-igd-gms", VFIOPCIDevice, igd_gms, 0),
    DEFINE_PROP_UNSIGNED_NODEFAULT("x-nv-gpudirect-clique", VFIOPCIDevice,
                                   nv_gpudirect_clique,
                                   qdev_prop_nv_gpudirect_clique, uint8_t),
    DEFINE_PROP_OFF_AUTO_PCIBAR("x-msix-relocation", VFIOPCIDevice, msix_relo,
                                OFF_AUTO_PCIBAR_OFF),
    DEFINE_PROP_LINK("iommufd", VFIOPCIDevice, vbasedev.iommufd,
                     TYPE_IOMMUFD_BACKEND, IOMMUFDBackend *),
    DEFINE_PROP_BOOL("skip-vsc-check", VFIOPCIDevice, skip_vsc_check, true),
    DEFINE_PROP_UINT16("x-vpasid-cap-offset", VFIOPCIDevice,
                       vpasid_cap_offset, 0),
    DEFINE_PROP_ON_OFF_AUTO("ats", VFIOPCIDevice, ats, ON_OFF_AUTO_AUTO),
};

static void vfio_pci_set_fd(Object *obj, const char *str, Error **errp)
{
    VFIOPCIDevice *vdev = VFIO_PCI_DEVICE(obj);
    vfio_device_set_fd(&vdev->vbasedev, str, errp);
}

static void vfio_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, vfio_pci_reset);
    device_class_set_props(dc, vfio_pci_properties);
    object_class_property_add_str(klass, "fd", NULL, vfio_pci_set_fd);
    dc->vmsd = &vfio_cpr_pci_vmstate;
    dc->desc = "VFIO-based PCI device assignment";
    pdc->realize = vfio_pci_realize;

    object_class_property_set_description(klass, /* 1.3 */
                                          "host",
                                          "Host PCI address [domain:]<bus:slot.function> of assigned device");
    object_class_property_set_description(klass, /* 1.3 */
                                          "x-intx-mmap-timeout-ms",
                                          "When EOI is not provided by KVM/QEMU, wait time "
                                          "(milliseconds) to re-enable device direct access "
                                          "after INTx (DEBUG)");
    object_class_property_set_description(klass, /* 1.5 */
                                          "x-vga",
                                          "Expose VGA address spaces for device");
    object_class_property_set_description(klass, /* 2.3 */
                                          "x-req",
                                          "Disable device request notification support (DEBUG)");
    object_class_property_set_description(klass, /* 2.4 and 2.5 */
                                          "x-no-mmap",
                                          "Disable MMAP for device. Allows to trace MMIO "
                                          "accesses (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-no-kvm-intx",
                                          "Disable direct VFIO->KVM INTx injection. Allows to "
                                          "trace INTx interrupts (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-no-kvm-msi",
                                          "Disable direct VFIO->KVM MSI injection. Allows to "
                                          "trace MSI interrupts (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-no-kvm-msix",
                                          "Disable direct VFIO->KVM MSIx injection. Allows to "
                                          "trace MSIx interrupts (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-pci-vendor-id",
                                          "Override PCI Vendor ID with provided value (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-pci-device-id",
                                          "Override PCI device ID with provided value (DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-pci-sub-vendor-id",
                                          "Override PCI Subsystem Vendor ID with provided value "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 2.5 */
                                          "x-pci-sub-device-id",
                                          "Override PCI Subsystem Device ID with provided value "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 2.6 */
                                          "sysfsdev",
                                          "Host sysfs path of assigned device");
    object_class_property_set_description(klass, /* 2.7 */
                                          "x-igd-opregion",
                                          "Expose host IGD OpRegion to guest");
    object_class_property_set_description(klass, /* 2.7 (See c4c45e943e51) */
                                          "x-igd-gms",
                                          "Override IGD data stolen memory size (32MiB units)");
    object_class_property_set_description(klass, /* 2.11 */
                                          "x-nv-gpudirect-clique",
                                          "Add NVIDIA GPUDirect capability indicating P2P DMA "
                                          "clique for device [0-15]");
    object_class_property_set_description(klass, /* 2.12 */
                                          "x-no-geforce-quirks",
                                          "Disable GeForce quirks (for NVIDIA Quadro/GRID/Tesla). "
                                          "Improves performance");
    object_class_property_set_description(klass, /* 2.12 */
                                          "display",
                                          "Enable display support for device, ex. vGPU");
    object_class_property_set_description(klass, /* 2.12 */
                                          "x-msix-relocation",
                                          "Specify MSI-X MMIO relocation to the end of specified "
                                          "existing BAR or new BAR to avoid virtualization overhead "
                                          "due to adjacent device registers");
    object_class_property_set_description(klass, /* 3.0 */
                                          "x-no-kvm-ioeventfd",
                                          "Disable registration of ioeventfds with KVM (DEBUG)");
    object_class_property_set_description(klass, /* 3.0 */
                                          "x-no-vfio-ioeventfd",
                                          "Disable linking of KVM ioeventfds to VFIO ioeventfds "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 3.1 */
                                          "x-balloon-allowed",
                                          "Override allowing ballooning with device (DEBUG, DANGER)");
    object_class_property_set_description(klass, /* 3.2 */
                                          "xres",
                                          "Set X display resolution the vGPU should use");
    object_class_property_set_description(klass, /* 3.2 */
                                          "yres",
                                          "Set Y display resolution the vGPU should use");
    object_class_property_set_description(klass, /* 5.2 */
                                          "x-pre-copy-dirty-page-tracking",
                                          "Disable dirty pages tracking during iterative phase "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 5.2, 8.0 non-experimetal */
                                          "enable-migration",
                                          "Enable device migration. Also requires a host VFIO PCI "
                                          "variant or mdev driver with migration support enabled");
    object_class_property_set_description(klass, /* 8.1 */
                                          "vf-token",
                                          "Specify UUID VF token. Required for VF when PF is owned "
                                          "by another VFIO driver");
    object_class_property_set_description(klass, /* 9.0 */
                                          "iommufd",
                                          "Set host IOMMUFD backend device");
    object_class_property_set_description(klass, /* 9.1 */
                                          "x-device-dirty-page-tracking",
                                          "Disable device dirty page tracking and use "
                                          "container-based dirty page tracking");
    object_class_property_set_description(klass, /* 9.1 */
                                          "migration-events",
                                          "Emit VFIO migration QAPI event when a VFIO device "
                                          "changes its migration state. For management applications");
    object_class_property_set_description(klass, /* 9.1 */
                                          "skip-vsc-check",
                                          "Skip config space check for Vendor Specific Capability. "
                                          "Setting to false will enforce strict checking of VSC content "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 10.0 */
                                          "x-migration-multifd-transfer",
                                          "Transfer this device state via "
                                          "multifd channels when live migrating it");
    object_class_property_set_description(klass, /* 10.1 */
                                          "x-migration-load-config-after-iter",
                                          "Start the config load only after "
                                          "all iterables were loaded (during "
                                          "non-iterables loading phase) when "
                                          "doing live migration of device state "
                                          "via multifd channels");
    object_class_property_set_description(klass, /* 10.1 */
                                          "x-migration-max-queued-buffers-size",
                                          "Maximum size of in-flight VFIO "
                                          "device state buffers queued at the "
                                          "destination when doing live "
                                          "migration of device state via "
                                          "multifd channels");
    object_class_property_set_description(klass, /* 11.0 */
                                          "x-vpasid-cap-offset",
                                          "PCIe extended configuration space offset at which to place a "
                                          "synthetic PASID extended capability when PASID is enabled via "
                                          "a vIOMMU. A value of 0 (default) places the capability at the "
                                          "end of the extended configuration space. The offset must be "
                                          "4-byte aligned and within the PCIe extended configuration space");
    object_class_property_set_description(klass, /* 11.1 */
                                          "ats",
                                          "Control guest visibility of the ATS PCIe extended capability. "
                                          "Valid values are on, off, and auto (default). "
                                          "'off' always masks ATS. "
                                          "'on' requires ATS support for the device and fails realize if the "
                                          "host kernel reports ATS as unavailable for this device. "
                                          "'auto' masks ATS only when the host kernel reports "
                                          "ATS as unavailable");
}

static const TypeInfo vfio_pci_info = {
    .name = TYPE_VFIO_PCI,
    .parent = TYPE_VFIO_PCI_DEVICE,
    .class_init = vfio_pci_class_init,
    .instance_init = vfio_pci_init,
    .instance_finalize = vfio_pci_finalize,
};

static const Property vfio_pci_nohotplug_properties[] = {
    DEFINE_PROP_BOOL("ramfb", VFIOPCIDevice, enable_ramfb, false),
    DEFINE_PROP_BOOL("use-legacy-x86-rom", VFIOPCIDevice,
                     use_legacy_x86_rom, false),
    DEFINE_PROP_ON_OFF_AUTO("x-ramfb-migrate", VFIOPCIDevice, ramfb_migrate,
                            ON_OFF_AUTO_AUTO),
};

static void vfio_pci_nohotplug_class_init(ObjectClass *klass,
                                              const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, vfio_pci_nohotplug_properties);
    dc->hotpluggable = false;

    object_class_property_set_description(klass, /* 3.1 */
                                          "ramfb",
                                          "Enable ramfb to provide pre-boot graphics for devices "
                                          "enabling display option");
    object_class_property_set_description(klass, /* 8.2 */
                                          "x-ramfb-migrate",
                                          "Override default migration support for ramfb support "
                                          "(DEBUG)");
    object_class_property_set_description(klass, /* 10.1 */
                                          "use-legacy-x86-rom",
                                          "Controls loading of a legacy VGA BIOS ROM");
}

static const TypeInfo vfio_pci_nohotplug_info = {
    .name = TYPE_VFIO_PCI_NOHOTPLUG,
    .parent = TYPE_VFIO_PCI,
    .instance_size = sizeof(VFIOPCIDevice),
    .class_init = vfio_pci_nohotplug_class_init,
};

static void register_vfio_pci_dev_type(void)
{
    /*
     * Ordinary ON_OFF_AUTO property isn't runtime-mutable, but source VM can
     * run for a long time before being migrated so it is desirable to have a
     * fallback mechanism to the old way of transferring VFIO device state if
     * it turns to be necessary.
     * The following makes this type of property have the same mutability level
     * as ordinary migration parameters.
     */
    vfio_pci_migration_multifd_transfer_prop = qdev_prop_on_off_auto;
    vfio_pci_migration_multifd_transfer_prop.realized_set_allowed = true;

    type_register_static(&vfio_pci_device_info);
    type_register_static(&vfio_pci_info);
    type_register_static(&vfio_pci_nohotplug_info);
}

type_init(register_vfio_pci_dev_type)
