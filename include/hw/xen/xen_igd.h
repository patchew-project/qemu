/*
 * Copyright (c) 2007, Neocleus Corporation.
 * Copyright (c) 2007, Intel Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Alex Novik <alex@neocleus.com>
 * Allen Kay <allen.m.kay@intel.com>
 * Guy Zana <guy@neocleus.com>
 */
#ifndef XEN_IGD_H
#define XEN_IGD_H

#define XEN_PCI_IGD_OPREGION 0xfc
#define XEN_PCI_IGD_OPREGION_MASK 0xfff
#define XEN_PCI_IGD_OPREGION_PAGES 0x2
#define XEN_PCI_IGD_OPREGION_ENABLE_ACCESSED 0x1
#define XEN_PCI_IGD_OPREGION_SIGNATURE "IntelGraphicsMem"
#define XEN_PCI_IGD_VBT_SIGNATURE "$VBT"
#define XEN_PCI_IGD_OPREGION_RVDA 0x3ba
#define XEN_PCI_IGD_OPREGION_RVDS 0x3c2
#define XEN_PCI_IGD_OPREGION_VERSION 0x16
#define XEN_PCI_IGD_DOMAIN 0
#define XEN_PCI_IGD_BUS 0
#define XEN_PCI_IGD_DEV 2
#define XEN_PCI_IGD_FN 0
#define XEN_PCI_IGD_SLOT_MASK \
    (1UL << PCI_SLOT(PCI_DEVFN(XEN_PCI_IGD_DEV, XEN_PCI_IGD_FN)))

#include "hw/xen/xen-host-pci-device.h"

typedef struct XenPCIPassthroughState XenPCIPassthroughState;

bool xen_igd_gfx_pt_enabled(void);
void xen_igd_gfx_pt_set(bool value, Error **errp);

uint32_t igd_read_opregion(XenPCIPassthroughState *s);
void xen_igd_reserve_slot(PCIBus *pci_bus);
void igd_write_opregion(XenPCIPassthroughState *s, uint32_t val);
void xen_igd_passthrough_isa_bridge_create(XenPCIPassthroughState *s,
                                           XenHostPCIDevice *dev,
                                           Error **errp);

static inline bool is_igd_vga_passthrough(XenHostPCIDevice *dev)
{
    return (xen_igd_gfx_pt_enabled()
            && ((dev->class_code >> 0x8) == PCI_CLASS_DISPLAY_VGA));
}

#endif
