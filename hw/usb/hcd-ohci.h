/*
 * QEMU USB OHCI Emulation
 * Copyright (c) 2004 Gianni Tedesco
 * Copyright (c) 2006 CodeSourcery
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HCD_OHCI_H
#define HCD_OHCI_H

#include "sysemu/dma.h"

/* Number of Downstream Ports on the root hub: */
#define OHCI_MAX_PORTS 15

typedef struct OHCIPort {
    USBPort port;
    uint32_t ctrl;
} OHCIPort;

typedef struct OHCIState {
    USBBus bus;
    qemu_irq irq;
    MemoryRegion mem;
    AddressSpace *as;
    uint32_t num_ports;
    const char *name;

    QEMUTimer *eof_timer;
    int64_t sof_time;

    /* OHCI state */
    /* Control partition */
    uint32_t ctl, status;
    uint32_t intr_status;
    uint32_t intr;

    /* memory pointer partition */
    uint32_t hcca;
    uint32_t ctrl_head, ctrl_cur;
    uint32_t bulk_head, bulk_cur;
    uint32_t per_cur;
    uint32_t done;
    int32_t done_count;

    /* Frame counter partition */
    uint16_t fsmps;
    uint8_t fit;
    uint16_t fi;
    uint8_t frt;
    uint16_t frame_number;
    uint16_t padding;
    uint32_t pstart;
    uint32_t lst;

    /* Root Hub partition */
    uint32_t rhdesc_a, rhdesc_b;
    uint32_t rhstatus;
    OHCIPort rhport[OHCI_MAX_PORTS];

    /* PXA27x Non-OHCI events */
    uint32_t hstatus;
    uint32_t hmask;
    uint32_t hreset;
    uint32_t htest;

    /* SM501 local memory offset */
    dma_addr_t localmem_base;

    /* Active packets.  */
    uint32_t old_ctl;
    USBPacket usb_packet;
    uint8_t usb_buf[8192];
    uint32_t async_td;
    bool async_complete;

    void (*ohci_die)(struct OHCIState *ohci);
} OHCIState;

extern const VMStateDescription vmstate_ohci_state;

#define OHCI_INTR_SO          (1U << 0)   /* Scheduling overrun */
#define OHCI_INTR_WD          (1U << 1)   /* HcDoneHead writeback */
#define OHCI_INTR_SF          (1U << 2)   /* Start of frame */
#define OHCI_INTR_RD          (1U << 3)   /* Resume detect */
#define OHCI_INTR_UE          (1U << 4)   /* Unrecoverable error */
#define OHCI_INTR_FNO         (1U << 5)   /* Frame number overflow */
#define OHCI_INTR_RHSC        (1U << 6)   /* Root hub status change */
#define OHCI_INTR_OC          (1U << 30)  /* Ownership change */
#define OHCI_INTR_MIE         (1U << 31)  /* Master Interrupt Enable */

/* Update IRQ levels */
static inline void ohci_intr_update(OHCIState *ohci)
{
    int level = 0;

    if ((ohci->intr & OHCI_INTR_MIE) &&
        (ohci->intr_status & ohci->intr))
        level = 1;

    qemu_set_irq(ohci->irq, level);
}

/* Set an interrupt */
static inline void ohci_set_interrupt(OHCIState *ohci, uint32_t intr)
{
    ohci->intr_status |= intr;
    ohci_intr_update(ohci);
}

void usb_ohci_init(OHCIState *ohci, DeviceState *dev, uint32_t num_ports,
                   dma_addr_t localmem_base, char *masterbus,
                   uint32_t firstport, AddressSpace *as,
                   void (*ohci_die_func)(OHCIState *ohci), Error **errp);
void ohci_hard_reset(OHCIState *ohci);
void ohci_bus_stop(OHCIState *ohci);
void ohci_stop_endpoints(OHCIState *ohci);

#endif
