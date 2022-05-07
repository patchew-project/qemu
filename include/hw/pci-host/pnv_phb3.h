/*
 * QEMU PowerPC PowerNV (POWER8) PHB3 model
 *
 * Copyright (c) 2014-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PCI_HOST_PNV_PHB3_H
#define PCI_HOST_PNV_PHB3_H

#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci-host/pnv_phb.h"
#include "hw/ppc/xics.h"
#include "qom/object.h"

typedef struct PnvChip PnvChip;

/*
 * PHB3 XICS Source for MSIs
 */
#define TYPE_PHB3_MSI "phb3-msi"
DECLARE_INSTANCE_CHECKER(Phb3MsiState, PHB3_MSI,
                         TYPE_PHB3_MSI)

void pnv_phb3_msi_update_config(Phb3MsiState *msis, uint32_t base,
                                uint32_t count);
void pnv_phb3_msi_send(Phb3MsiState *msis, uint64_t addr, uint16_t data,
                       int32_t dev_pe);
void pnv_phb3_msi_ffi(Phb3MsiState *msis, uint64_t val);
void pnv_phb3_msi_pic_print_info(Phb3MsiState *msis, Monitor *mon);

/*
 * PHB3 Power Bus Common Queue
 */
#define TYPE_PNV_PBCQ "pnv-pbcq"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPBCQState, PNV_PBCQ)

/*
 * PHB3 PCIe Root port
 */
#define TYPE_PNV_PHB3_ROOT_BUS "pnv-phb3-root"

#define TYPE_PNV_PHB3_ROOT_PORT "pnv-phb3-root-port"

typedef struct PnvPHB3RootPort {
    PCIESlot parent_obj;
} PnvPHB3RootPort;


#define PNV_PHB3_NUM_M64      16
#define PNV_PHB3_NUM_REGS     (0x1000 >> 3)
#define PNV_PHB3_NUM_LSI      8
#define PNV_PHB3_NUM_PE       256

#define PCI_MMIO_TOTAL_SIZE   (0x1ull << 60)


uint64_t pnv_phb3_reg_read(void *opaque, hwaddr off, unsigned size);
void pnv_phb3_reg_write(void *opaque, hwaddr off, uint64_t val, unsigned size);
void pnv_phb3_update_regions(PnvPHB *phb);
void pnv_phb3_remap_irqs(PnvPHB *phb);

void pnv_phb3_instance_init(Object *obj);
void pnv_phb3_realize(DeviceState *dev, Error **errp);

#endif /* PCI_HOST_PNV_PHB3_H */
