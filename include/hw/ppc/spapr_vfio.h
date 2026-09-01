/*
 * sPAPR VFIO EEH Header
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_PPC_SPAPR_VFIO_H
#define HW_PPC_SPAPR_VFIO_H

/*
 * Forward declarations to avoid pulling in full spapr headers
 * This allows stubs and other files to compile without libfdt dependencies
 */
typedef struct SpaprPhbState SpaprPhbState;
typedef struct DeviceState DeviceState;

/* VFIO EEH function declarations */
bool spapr_phb_eeh_available(SpaprPhbState *sphb);
int spapr_phb_vfio_eeh_set_option(SpaprPhbState *sphb,
                                  unsigned int addr, int option);
int spapr_phb_vfio_eeh_get_state(SpaprPhbState *sphb, int *state);
int spapr_phb_vfio_eeh_reset(SpaprPhbState *sphb, int option);
int spapr_phb_vfio_eeh_configure(SpaprPhbState *sphb);
void spapr_phb_vfio_reset(DeviceState *qdev);
void spapr_phb_vfio_eeh_reenable(SpaprPhbState *sphb);
int spapr_phb_vfio_errinjct(SpaprPhbState *sphb, uint32_t type,
                            uint32_t func, uint64_t addr, uint64_t mask);
int spapr_phb_vfio_translate_errinjct_addr(SpaprPhbState *sphb,
                                           uint32_t config_addr,
                                           uint64_t guest_addr,
                                           uint64_t *host_pci_bus_addr);
#endif /* HW_PPC_SPAPR_VFIO_H */
