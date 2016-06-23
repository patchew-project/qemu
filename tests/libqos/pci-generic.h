/*
 * libqos PCI bindings for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Prem Mallappa     <prem.mallappa@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_PCI_PC_H
#define LIBQOS_PCI_PC_H

#include "libqos/pci.h"

typedef struct QPCIBusGen
{
    QPCIBus bus;

    uint64_t base;
    uint32_t pci_hole_start;
    uint32_t pci_hole_size;
    uint32_t pci_hole_alloc;

    uint16_t pci_iohole_start;
    uint16_t pci_iohole_size;
    uint16_t pci_iohole_alloc;
} QPCIBusGen;

QPCIBus *qpci_init_generic(QPCIBusGen *);
void     qpci_free_generic(QPCIBus *bus);
#if 0
uint8_t qpci_generic_io_readb(QPCIBus *bus, void *addr);
uint16_t qpci_generic_io_readw(QPCIBus *bus, void *addr);
uint32_t qpci_generic_io_readl(QPCIBus *bus, void *addr);

void qpci_generic_io_writeb(QPCIBus *bus, void *addr, uint8_t value);
void qpci_generic_io_writew(QPCIBus *bus, void *addr, uint16_t value);
void qpci_generic_io_writel(QPCIBus *bus, void *addr, uint32_t value);

uint8_t qpci_generic_config_readb(QPCIBus *bus, int devfn, uint8_t offset);
uint16_t qpci_generic_config_readw(QPCIBus *bus, int devfn, uint8_t offset);
uint32_t qpci_generic_config_readl(QPCIBus *bus, int devfn, uint8_t offset);

void qpci_generic_config_writeb(QPCIBus *bus, int devfn,
                      uint8_t offset, uint8_t value);
void qpci_generic_config_writew(QPCIBus *bus, int devfn,
                      uint8_t offset, uint16_t value);
void qpci_generic_config_writel(QPCIBus *bus, int devfn,
                      uint8_t offset, uint32_t value);

void * qpci_generic_iomap(QPCIBus *bus, QPCIDevice *dev, int barno, uint64_t *sizeptr);
void qpci_genericiounmap(QPCIBus *bus, void *data);
#endif
#endif
