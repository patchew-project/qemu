/*
 * Flattened Image Tree loader.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "hw/loader.h"
#include "hw/loader-fit.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"

#include <libfdt.h>
#include <zlib.h>

static const void *fit_load_image(const void *itb, const char *name,
                                  int *poff, size_t *psz)
{
    const void *data;
    const char *comp;
    void *uncomp_data;
    char path[128];
    int off, sz;
    ssize_t uncomp_len;

    snprintf(path, sizeof(path), "/images/%s", name);

    off = fdt_path_offset(itb, path);
    if (off < 0) {
        return NULL;
    }
    if (poff) {
        *poff = off;
    }

    data = fdt_getprop(itb, off, "data", &sz);
    if (!data) {
        return NULL;
    }

    comp = fdt_getprop(itb, off, "compression", NULL);
    if (!comp || !strcmp(comp, "none")) {
        if (psz) {
            *psz = sz;
        }
        return data;
    }

    if (!strcmp(comp, "gzip")) {
        uncomp_len = 64 << 20;
        uncomp_data = g_malloc(uncomp_len);

        uncomp_len = gunzip(uncomp_data, uncomp_len, (void *)data, sz);
        if (uncomp_len < 0) {
            error_printf("unable to decompress %s image\n", name);
            g_free(uncomp_data);
            return NULL;
        }

        data = g_realloc(uncomp_data, uncomp_len);
        if (psz) {
            *psz = uncomp_len;
        }
        return data;
    }

    error_printf("unknown compression '%s'\n", comp);
    return NULL;
}

static int fit_image_addr(const void *itb, int img, const char *name,
                          hwaddr *addr)
{
    const void *prop;
    int len;

    prop = fdt_getprop(itb, img, name, &len);
    if (!prop) {
        return -ENOENT;
    }

    switch (len) {
    case 4:
        *addr = fdt32_to_cpu(*(fdt32_t *)prop);
        return 0;
    case 8:
        *addr = fdt64_to_cpu(*(fdt64_t *)prop);
        return 0;
    default:
        error_printf("invalid %s address length %d\n", name, len);
        return -EINVAL;
    }
}

static int fit_load_kernel(const struct fit_loader *ldr, const void *itb,
                           int cfg, void *opaque, hwaddr *pend)
{
    const char *name;
    const void *data;
    hwaddr load_addr, entry_addr;
    int img_off, err;
    size_t sz;

    name = fdt_getprop(itb, cfg, "kernel", NULL);
    if (!name) {
        error_printf("no kernel specified by FIT configuration\n");
        return -EINVAL;
    }

    data = fit_load_image(itb, name, &img_off, &sz);
    if (!data) {
        error_printf("unable to load kernel image from FIT\n");
        return -EINVAL;
    }

    err = fit_image_addr(itb, img_off, "load", &load_addr);
    if (err) {
        error_printf("unable to read kernel load address from FIT\n");
        return err;
    }

    err = fit_image_addr(itb, img_off, "entry", &entry_addr);
    if (err) {
        return err;
    }

    if (ldr->kernel_filter) {
        data = ldr->kernel_filter(opaque, data, &load_addr, &entry_addr);
    }

    if (pend) {
        *pend = load_addr + sz;
    }

    load_addr = ldr->addr_to_phys(opaque, load_addr);
    rom_add_blob_fixed(name, data, sz, load_addr);

    return 0;
}

static int fit_load_fdt(const struct fit_loader *ldr, const void *itb,
                        int cfg, void *opaque, const void *match_data,
                        hwaddr kernel_end)
{
    const char *name;
    const void *data;
    hwaddr load_addr;
    int img_off, err;
    size_t sz;

    name = fdt_getprop(itb, cfg, "fdt", NULL);
    if (!name) {
        return 0;
    }

    data = fit_load_image(itb, name, &img_off, &sz);
    if (!data) {
        error_printf("unable to load FDT image from FIT\n");
        return -EINVAL;
    }

    err = fit_image_addr(itb, img_off, "load", &load_addr);
    if (err == -ENOENT) {
        load_addr = ROUND_UP(kernel_end, 64 * K_BYTE) + (10 * M_BYTE);
    } else if (err) {
        return err;
    }

    if (ldr->fdt_filter) {
        data = ldr->fdt_filter(opaque, data, match_data, &load_addr);
    }

    load_addr = ldr->addr_to_phys(opaque, load_addr);
    sz = fdt_totalsize(data);
    rom_add_blob_fixed(name, data, sz, load_addr);

    return 0;
}

static bool fit_cfg_compatible(const void *itb, int cfg, const char *compat)
{
    const void *fdt;
    const char *fdt_name;

    fdt_name = fdt_getprop(itb, cfg, "fdt", NULL);
    if (!fdt_name) {
        return false;
    }

    fdt = fit_load_image(itb, fdt_name, NULL, NULL);
    if (!fdt) {
        return false;
    }

    if (fdt_check_header(fdt)) {
        return false;
    }

    if (fdt_node_check_compatible(fdt, 0, compat)) {
        return false;
    }

    return true;
}

int load_fit(const struct fit_loader *ldr, const char *filename, void *opaque)
{
    const struct fit_loader_match *match;
    const void *itb, *match_data = NULL;
    const char *def_cfg_name;
    char path[128];
    int itb_size, configs, cfg_off, off, err;
    hwaddr kernel_end;

    itb = load_device_tree(filename, &itb_size);
    if (!itb) {
        return -EINVAL;
    }

    configs = fdt_path_offset(itb, "/configurations");
    if (configs < 0) {
        return configs;
    }

    cfg_off = -FDT_ERR_NOTFOUND;

    if (ldr->matches) {
        for (match = ldr->matches; match->compatible; match++) {
            off = fdt_first_subnode(itb, configs);
            while (off >= 0) {
                if (fit_cfg_compatible(itb, off, match->compatible)) {
                    cfg_off = off;
                    match_data = match->data;
                    break;
                }

                off = fdt_next_subnode(itb, off);
            }

            if (cfg_off >= 0) {
                break;
            }
        }
    }

    if (cfg_off < 0) {
        def_cfg_name = fdt_getprop(itb, configs, "default", NULL);
        if (def_cfg_name) {
            snprintf(path, sizeof(path), "/configurations/%s", def_cfg_name);
            cfg_off = fdt_path_offset(itb, path);
        }
    }

    if (cfg_off < 0) {
        /* couldn't find a configuration to use */
        return cfg_off;
    }

    err = fit_load_kernel(ldr, itb, cfg_off, opaque, &kernel_end);
    if (err) {
        return err;
    }

    err = fit_load_fdt(ldr, itb, cfg_off, opaque, match_data, kernel_end);
    if (err) {
        return err;
    }

    return 0;
}
