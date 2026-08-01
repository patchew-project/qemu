/*
 * This is splited from hw/i386/kvm/pci-assign.c
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/pci/pci.h"
#include "xen_pt.h"

/*
 * Normally xen_pt_register_regions will handle loading the option ROM,
 * but in some cases, such as for the Intel IGD, the option ROM might
 * need to be modified.
 *
 * For such cases, use this function to get a pointer to the option ROM
 * from sysfs. Caller has the responsibility to edit the option ROM as
 * needed, call pci_register_bar to register the modified option ROM,
 * and set has_rom to true for the PCI device.
 *
 * This function must be called before xen_pt_register_regions is called
 * because if xen_pt_register_regions is called first, it will register
 * the option ROM and any attempt to register it again will fail.
 */
void *pci_assign_dev_load_option_rom(PCIDevice *dev,
                                     int *size, unsigned int domain,
                                     unsigned int bus, unsigned int slot,
                                     unsigned int function)
{
    char name[32], rom_file[64];
    FILE *fp;
    uint8_t val;
    struct stat st;
    void *ptr = NULL;
    Object *owner = OBJECT(dev);

    /* If loading ROM from file, pci handles it */
    if (dev->romfile || !dev->rom_bar) {
        return NULL;
    }

    snprintf(rom_file, sizeof(rom_file),
             "/sys/bus/pci/devices/%04x:%02x:%02x.%01x/rom",
             domain, bus, slot, function);

    /* Write "1" to the ROM file to enable it */
    fp = fopen(rom_file, "r+");
    if (fp == NULL) {
        if (errno != ENOENT) {
            error_report("pci-assign: Cannot open %s: %s", rom_file, strerror(errno));
        }
        return NULL;
    }
    if (fstat(fileno(fp), &st) == -1) {
        error_report("pci-assign: Cannot stat %s: %s", rom_file, strerror(errno));
        goto close_rom;
    }

    val = 1;
    if (fwrite(&val, 1, 1, fp) != 1) {
        goto close_rom;
    }
    fseek(fp, 0, SEEK_SET);

    if (dev->romsize != UINT_MAX) {
        if (st.st_size > dev->romsize) {
            error_report("ROM BAR \"%s\" (%ld bytes) is too large for ROM size %u",
                         rom_file, (long) st.st_size, dev->romsize);
            goto close_rom;
        }
    } else {
        dev->romsize = st.st_size;
    }

    snprintf(name, sizeof(name), "%s.rom", object_get_typename(owner));
    memory_region_init_ram(&dev->rom, owner, name, dev->romsize, &error_abort);
    ptr = memory_region_get_ram_ptr(&dev->rom);
    memset(ptr, 0xff, dev->romsize);

    if (!fread(ptr, 1, st.st_size, fp)) {
        info_report("pci-assign: Cannot read Option ROM %s from host", rom_file);
        goto close_rom;
    }

    *size = st.st_size;
close_rom:
    /* Write "0" to disable ROM */
    fseek(fp, 0, SEEK_SET);
    val = 0;
    if (!fwrite(&val, 1, 1, fp)) {
        XEN_PT_WARN(dev, "%s\n", "Failed to disable pci-sysfs rom file");
    }
    fclose(fp);

    return ptr;
}
