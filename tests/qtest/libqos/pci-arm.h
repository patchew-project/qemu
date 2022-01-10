/*
 * libqos PCI bindings for ARM
 *
 * Copyright Red Hat Inc., 2021
 *
 * Authors:
 *  Eric Auger   <eric.auger@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_PCI_ARM_H
#define LIBQOS_PCI_ARM_H

#include "pci.h"
#include "malloc.h"
#include "qgraph.h"

typedef struct QPCIBusARM {
    QOSGraphObject obj;
    QPCIBus bus;
    uint64_t gpex_pio_base;
} QPCIBusARM;

/*
 * qpci_init_arm():
 * @ret: A valid QPCIBusARM * pointer
 * @qts: The %QTestState for this ARM machine
 * @alloc: A previously initialized @alloc providing memory for @qts
 * @bool: devices can be hotplugged on this bus
 *
 * This function initializes an already allocated
 * QPCIBusARM object.
 */
void qpci_init_arm(QPCIBusARM *ret, QTestState *qts,
                   QGuestAllocator *alloc, bool hotpluggable);

/*
 * qpci_arm_new():
 * @qts: The %QTestState for this ARM machine
 * @alloc: A previously initialized @alloc providing memory for @qts
 * @hotpluggable: the pci bus is hotpluggable
 *
 * This function creates a new QPCIBusARM object,
 * and properly initialize its fields.
 *
 * Returns the QPCIBus *bus field of a newly
 * allocated QPCIBusARM.
 */
QPCIBus *qpci_new_arm(QTestState *qts, QGuestAllocator *alloc,
                      bool hotpluggable);

void qpci_free_arm(QPCIBus *bus);

#endif
