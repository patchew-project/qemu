/*
 * QEMU sPAPR PCI host for VFIO - non-EEH stubs
 *
 * Copyright (c) 2011-2014 Alexey Kardashevskiy, IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file provides stub implementations of the sPAPR EEH/errinjct
 * functions for Linux builds where CONFIG_VFIO_PCI is not enabled.
 * The real implementations live in spapr_eeh.c, which is compiled
 * only when CONFIG_VFIO_PCI is enabled.
 */

#include "qemu/osdep.h"
#include "hw/ppc/spapr.h"
#include "hw/pci-host/spapr.h"

bool spapr_phb_eeh_available(SpaprPhbState *sphb)
{
    return false;
}

void spapr_phb_vfio_reset(DeviceState *qdev)
{
}

int spapr_phb_vfio_eeh_set_option(SpaprPhbState *sphb,
                                  unsigned int addr, int option)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

int spapr_phb_vfio_eeh_get_state(SpaprPhbState *sphb, int *state)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

int spapr_phb_vfio_eeh_reset(SpaprPhbState *sphb, int option)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

int spapr_phb_vfio_eeh_configure(SpaprPhbState *sphb)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

int spapr_phb_vfio_errinjct(SpaprPhbState *sphb, uint32_t type,
                            uint32_t func, uint64_t addr, uint64_t mask)
{
    return RTAS_OUT_NOT_SUPPORTED;
}

int spapr_phb_vfio_translate_errinjct_addr(SpaprPhbState *sphb,
                                           uint32_t config_addr,
                                           uint64_t guest_addr,
                                           uint64_t *host_pci_bus_addr)
{
    return -ENOTSUP;
}
