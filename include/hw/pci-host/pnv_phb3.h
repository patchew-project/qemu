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

typedef struct PnvPHB3 PnvPHB3;
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

/*
 * PHB3 PCIe Host Bridge for PowerNV machines (POWER8)
 */
#define TYPE_PNV_PHB3 "pnv-phb3"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPHB3, PNV_PHB3)

#define PNV_PHB3_NUM_M64      16
#define PNV_PHB3_NUM_REGS     (0x1000 >> 3)
#define PNV_PHB3_NUM_LSI      8
#define PNV_PHB3_NUM_PE       256

#define PCI_MMIO_TOTAL_SIZE   (0x1ull << 60)

struct PnvPHB3 {
    PCIExpressHost parent_obj;

    uint32_t chip_id;
    uint32_t phb_id;
    char bus_path[8];

    uint64_t regs3[PNV_PHB3_NUM_REGS];
    MemoryRegion mr_regs3;

    MemoryRegion mr_m32;
    MemoryRegion mr_m64[PNV_PHB3_NUM_M64];
    MemoryRegion pci_mmio;
    MemoryRegion pci_io;

    uint64_t ioda2_LIST[8];
    uint64_t ioda2_LXIVT[8];
    uint64_t ioda2_TVT[512];
    uint64_t ioda2_M64BT[16];
    uint64_t ioda2_MDT[256];
    uint64_t ioda2_PEEV[4];

    uint32_t total_irq;
    ICSState lsis;
    qemu_irq *qirqs;
    Phb3MsiState msis;

    PnvPBCQState pbcq;

    QLIST_HEAD(, PnvPhb3DMASpace) v3_dma_spaces;

    PnvChip *chip;
};

uint64_t pnv_phb3_reg_read(void *opaque, hwaddr off, unsigned size);
void pnv_phb3_reg_write(void *opaque, hwaddr off, uint64_t val, unsigned size);
void pnv_phb3_update_regions(PnvPHB3 *phb);
void pnv_phb3_remap_irqs(PnvPHB3 *phb);

void pnv_phb3_instance_init(Object *obj);
void pnv_phb3_realize(DeviceState *dev, Error **errp);

#endif /* PCI_HOST_PNV_PHB3_H */
