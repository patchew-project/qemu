/*
 * s390 vfio-pci interfaces
 *
 * Copyright 2020 IBM Corp.
 * Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_PCI_VFIO_H
#define HW_S390_PCI_VFIO_H

#include "hw/s390x/s390-pci-bus.h"
#include CONFIG_DEVICES

#ifdef CONFIG_VFIO
bool s390_pci_update_dma_avail(int fd, unsigned int *avail);
S390PCIDMACount *s390_pci_start_dma_count(S390pciState *s,
                                          S390PCIBusDevice *pbdev);
void s390_pci_end_dma_count(S390pciState *s, S390PCIDMACount *cnt);
void s390_pci_get_clp_info(S390PCIBusDevice *pbdev);
int s390_pci_get_zpci_io_region(S390PCIBusDevice *pbdev);
int s390_pci_vfio_pcilg(S390PCIBusDevice *pbdev, uint64_t *data, uint8_t pcias,
                        uint16_t len, uint64_t offset);
int s390_pci_vfio_pcistb(S390PCIBusDevice *pbdev, S390CPU *cpu, uint64_t gaddr,
                         uint8_t ar, uint8_t pcias, uint16_t len,
                         uint64_t offset);
#else
static inline bool s390_pci_update_dma_avail(int fd, unsigned int *avail)
{
    return false;
}
static inline S390PCIDMACount *s390_pci_start_dma_count(S390pciState *s,
                                                        S390PCIBusDevice *pbdev)
{
    return NULL;
}
static inline void s390_pci_end_dma_count(S390pciState *s,
                                          S390PCIDMACount *cnt) { }
static inline void s390_pci_get_clp_info(S390PCIBusDevice *pbdev) { }
static inline int s390_pci_get_zpci_io_region(S390PCIBusDevice *pbdev)
{
    return -EINVAL;
}
static inline int s390_pci_vfio_pcilg(S390PCIBusDevice *pbdev, uint64_t *data,
                                      uint8_t pcias, uint16_t len,
                                      uint64_t offset)
{
    return -EIO;
}
static inline int s390_pci_vfio_pcistb(S390PCIBusDevice *pbdev, S390CPU *cpu,
                                       uint64_t gaddr, uint8_t ar,
                                       uint8_t pcias, uint16_t len,
                                       uint64_t offset)
{
    return -EIO;
}
#endif

#endif
