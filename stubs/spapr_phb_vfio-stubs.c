/*
 * Stubs for sPAPR PCI VFIO EEH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/ppc/spapr_vfio.h"

/* RTAS return codes */
#define RTAS_OUT_NOT_SUPPORTED          (-3)


bool spapr_phb_eeh_available(SpaprPhbState *sphb)
{
    return false;
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

void spapr_phb_vfio_reset(DeviceState *qdev)
{
}

void spapr_phb_vfio_eeh_reenable(SpaprPhbState *sphb)
{
}

int spapr_phb_vfio_errinjct(SpaprPhbState *sphb, uint32_t func,
                            uint64_t addr, uint64_t mask, uint32_t type)
{
    return RTAS_OUT_NOT_SUPPORTED;
}
