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

#include "qapi/error.h"
#include "qemu/error-report.h"

typedef struct {
    hwaddr ram_start;
    size_t ram_size;
    hwaddr entry;
    hwaddr kernel_entry;
    hwaddr dtb_address;
    const char *filename;
    const char *dtbname;
    const char *cmdline;
} rx_kernel_info_t;

bool load_bios(const char *filename, int rom_size, Error **errp);

bool load_kernel(rx_kernel_info_t *info);
