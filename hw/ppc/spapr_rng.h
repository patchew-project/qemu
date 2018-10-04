/*
 * QEMU sPAPR random number generator "device" for H_RANDOM hypercall
 *
 * Copyright 2015 Thomas Huth, Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SPAPR_RNG_H
#define SPAPR_RNG_H

#define TYPE_SPAPR_RNG "spapr-rng"

static inline int spapr_rng_populate_dt(void *fdt)
{
    int node;
    int ret;

    node = qemu_fdt_add_subnode(fdt, "/ibm,platform-facilities");
    if (node <= 0) {
        return -1;
    }
    ret = fdt_setprop_string(fdt, node, "device_type",
                             "ibm,platform-facilities");
    ret |= fdt_setprop_cell(fdt, node, "#address-cells", 0x1);
    ret |= fdt_setprop_cell(fdt, node, "#size-cells", 0x0);

    node = fdt_add_subnode(fdt, node, "ibm,random-v1");
    if (node <= 0) {
        return -1;
    }
    ret |= fdt_setprop_string(fdt, node, "compatible", "ibm,random");

    return ret ? -1 : 0;
}

#endif
