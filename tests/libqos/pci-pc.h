/*
 * libqos PCI bindings for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_PCI_PC_H
#define LIBQOS_PCI_PC_H

#include "libqos/pci.h"
#include "libqos/malloc.h"
#include "qgraph.h"

typedef struct QPCIBusPC {
    QOSGraphObject obj;
    QPCIBus bus;
} QPCIBusPC;

void qpci_device_init(QPCIDevice *dev, QPCIBus *bus, int devfn);
void qpci_set_pc(QPCIBusPC *ret, QTestState *qts, QGuestAllocator *alloc);
QPCIBus *qpci_init_pc(QTestState *qts, QGuestAllocator *alloc);
void     qpci_free_pc(QPCIBus *bus);

#endif
