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
#include "libqos/qgraph.h"

typedef struct QPCIBusPC {
    QOSGraphObject obj;
    QPCIBus bus;
} QPCIBusPC;

/* qpci_init_pc():
 * this function initialize an already allocated
 * QPCIBusPC object.
 *
 * @ret must be a valid QPCIBusPC * pointer.
 */
void qpci_init_pc(QPCIBusPC *ret, QTestState *qts, QGuestAllocator *alloc);
/* qpci_pc_new():
 * this function creates a new QPCIBusPC object,
 * and properly initialize its fields.
 *
 * returns the QPCIBus *bus field of a newly
 * allocated QPCIBusPC.
 */
QPCIBus *qpci_new_pc(QTestState *qts, QGuestAllocator *alloc);
void     qpci_free_pc(QPCIBus *bus);

#endif
