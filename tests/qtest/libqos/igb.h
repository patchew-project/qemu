/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef QGRAPH_IGB_H
#define QGRAPH_IGB_H

#include "qgraph.h"
#include "pci.h"

#define IGB_NUM_QUEUES            (8)
#define IGB_RX0_MSIX_VEC          (0)
#define IGB_TX0_MSIX_VEC          (0)
#define IGB_RX1_MSIX_VEC          (1)
#define IGB_TX1_MSIX_VEC          (1)
#define IGB_IVAR_ENTRY_VALID(x) ((x) & 0x80)

typedef struct QIGB QIGB;
typedef struct QIGB_PCI QIGB_PCI;

struct QIGB {
    uint64_t tx_ring[IGB_NUM_QUEUES];
    uint64_t rx_ring[IGB_NUM_QUEUES];
};

struct QIGB_PCI {
    QOSGraphObject obj;
    QPCIDevice pci_dev;
    QPCIBar mac_regs;
    QIGB igb;
};

void igb_wait_isr(QIGB *d, uint16_t msg_id);
void igb_tx_ring_push(QIGB *d, void *descr, uint8_t queue_index);
void igb_rx_ring_push(QIGB *d, void *descr, uint8_t queue_index);

#endif
