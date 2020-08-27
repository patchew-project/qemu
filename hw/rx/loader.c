/*
 * RX QEMU frimware loader
 *
 * Copyright (c) 2020 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "elf.h"
#include "hw/loader.h"
#include "hw/loader-fit.h"
#include "hw/rx/loader.h"
#include "sysemu/device_tree.h"
#include "exec/cpu-defs.h"
#include <libfdt.h>

#define RX_RESET_VEC 0xfffffffc
#define ADDRESS_TOP ((1LL << TARGET_PHYS_ADDR_SPACE_BITS) - 1)

bool load_bios(const char *filename, int rom_size, Error **errp)
{
    int size;
    uint64_t entry64 = UINT64_MAX;
    uint32_t entry;

    size = load_elf(filename, NULL, NULL, NULL, &entry64,
                    NULL, NULL, NULL, 0, EM_RX, 0, 0);
    if (size > 0) {
        goto load_ok;
    }
    size = load_targphys_hex_as(filename, &entry64, NULL);
    if (size > 0) {
        goto load_ok;
    }
    size = load_targphys_srec_as(filename, &entry64, NULL);
    if (size > 0) {
        goto load_ok;
    }
    size = get_image_size(filename);
    if (size < 0) {
        error_setg(errp, "\"%s\" is open failed.", filename);
        return false;
    }
    if (size > rom_size) {
        error_setg(errp, "\"%s\" is too large for ROM area.", filename);
        return false;
    }

    /*
     * The RX CPU reset vector is at the top of the ROM,
     * so the raw binary is loaded there.
     */
    rom_add_file_fixed(filename, -size, 0);
 load_ok:
    if (rom_ptr(RX_RESET_VEC, 4) == NULL) {
        if (entry64 <= ADDRESS_TOP) {
            entry = cpu_to_le32(entry64);
            rom_add_blob_fixed("entry", &entry, 4, RX_RESET_VEC);
        } else {
            error_setg(errp, "Reset vector is not set");
            return false;
        }
    }
    return true;
}

static hwaddr rx_addr_to_phys(void *opaque, uint64_t addr)
{
    /* No address translation */
    return addr;
}

static bool setup_commandline(void *dtb, rx_kernel_info_t *info)
{
    if (info->cmdline &&
        qemu_fdt_setprop_string(dtb, "/chosen", "bootargs",
                                info->cmdline) < 0) {
        return false;
    }
    return true;
}


static const void *rx_fdt_filter(void *opaque, const void *fdt_orig,
                                 const void *match_data, hwaddr *load_addr)
{
    rx_kernel_info_t *info = opaque;
    void *fdt;
    size_t fdt_sz;
    int err;

    fdt_sz = fdt_totalsize(fdt_orig) + 0x1000;
    fdt = g_malloc0(fdt_sz);

    err = fdt_open_into(fdt_orig, fdt, fdt_sz);
    if (err) {
        error_report("couldn't open dtb");
        return NULL;
    }

    if (!setup_commandline(fdt, info)) {
        error_report("couldn't set /chosen/bootargs");
        return NULL;
    }
    fdt_sz = fdt_totalsize(fdt);
    fdt = g_realloc(fdt, fdt_totalsize(fdt));
    info->dtb_address = info->ram_start + info->ram_size - fdt_sz;
    *load_addr = info->dtb_address;

    return fdt;
}

static const void *rx_kernel_filter(void *opaque, const void *kernel,
                                        hwaddr *load_addr, hwaddr *entry_addr)
{
    rx_kernel_info_t *info = opaque;

    info->kernel_entry = *entry_addr;

    return kernel;
}

static const struct fit_loader rx_fit_loader = {
    .addr_to_phys = rx_addr_to_phys,
    .fdt_filter = rx_fdt_filter,
    .kernel_filter = rx_kernel_filter,
};

bool load_kernel(rx_kernel_info_t *info)
{
    ram_addr_t kernel_offset;
    size_t kernel_size;

    if (load_fit(&rx_fit_loader, info->filename, info) == 0) {
        return true;
    }

    /*
     * The kernel image is loaded into
     * the latter half of the SDRAM space.
     */
    kernel_offset = info->ram_size / 2;

    info->entry = info->ram_start + kernel_offset;
    kernel_size = load_image_targphys(info->filename,
                                      info->entry, info->ram_size / 2);
    if (kernel_size == -1) {
        return false;
    }
    if (info->dtbname) {
        ram_addr_t dtb_offset;
        int dtb_size;
        void *dtb;

        dtb = load_device_tree(info->dtbname, &dtb_size);
        if (dtb == NULL) {
            error_report("Couldn't open dtb file %s", info->dtbname);
            return false;
        }
        if (!setup_commandline(dtb, info)) {
            error_report("Couldn't set /chosen/bootargs");
            return false;
        }
        /* DTB is located at the end of SDRAM space. */
        dtb_size = fdt_totalsize(dtb);
        dtb_offset = info->ram_size - dtb_size;
        info->dtb_address = info->ram_start + dtb_offset;
        rom_add_blob_fixed("dtb", dtb, dtb_size, info->dtb_address);
    }
    return true;
}
