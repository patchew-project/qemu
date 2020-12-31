/*
 * Algorithmics Bonito64 'north bridge' controller
 *
 * Copyright (c) 2008 yajin (yajin@vm-kernel.org)
 * Copyright (c) 2010 Huacai Chen (zltjiangshi@gmail.com)
 * Copyright (c) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_PCI_HOST_BONITO_H
#define HW_PCI_HOST_BONITO_H

#include "exec/memory.h"
#include "hw/pci/pci_host.h"
#include "qom/object.h"

typedef struct BonitoPciState BonitoPciState;

#define TYPE_BONITO_PCI_HOST_BRIDGE "Bonito-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(BonitoState, BONITO_PCI_HOST_BRIDGE)

typedef struct BonitoState BonitoState;

struct BonitoState {
    /*< private >*/
    PCIHostState parent_obj;
    /*< public >*/
    qemu_irq *pic;
    BonitoPciState *pci_dev;
    MemoryRegion pci_mem;
    MemoryRegion pcimem_lo_alias[3];
};

#endif
