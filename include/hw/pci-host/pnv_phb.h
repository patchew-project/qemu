/*
 * QEMU PowerPC PowerNV Unified PHB model
 *
 * Copyright (c) 2022, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PCI_HOST_PNV_PHB_H
#define PCI_HOST_PNV_PHB_H

#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/ppc/xics.h"
#include "hw/ppc/xive.h"
#include "qom/object.h"

/* pnv_phb3.h types */
#define PNV_PHB3_NUM_M64      16
#define PNV_PHB3_NUM_REGS     (0x1000 >> 3)
#define PHB3_MAX_MSI     2048

typedef struct PnvChip PnvChip;
typedef struct PnvPHB PnvPHB;

typedef struct Phb3MsiState {
    ICSState ics;
    qemu_irq *qirqs;

    PnvPHB *phb;
    uint64_t rba[PHB3_MAX_MSI / 64];
    uint32_t rba_sum;
} Phb3MsiState;

typedef struct PnvPBCQState {
    DeviceState parent;

    uint32_t nest_xbase;
    uint32_t spci_xbase;
    uint32_t pci_xbase;
#define PBCQ_NEST_REGS_COUNT    0x46
#define PBCQ_PCI_REGS_COUNT     0x15
#define PBCQ_SPCI_REGS_COUNT    0x5

    uint64_t nest_regs[PBCQ_NEST_REGS_COUNT];
    uint64_t spci_regs[PBCQ_SPCI_REGS_COUNT];
    uint64_t pci_regs[PBCQ_PCI_REGS_COUNT];
    MemoryRegion mmbar0;
    MemoryRegion mmbar1;
    MemoryRegion phbbar;
    uint64_t mmio0_base;
    uint64_t mmio0_size;
    uint64_t mmio1_base;
    uint64_t mmio1_size;
    PnvPHB *phb;

    MemoryRegion xscom_nest_regs;
    MemoryRegion xscom_pci_regs;
    MemoryRegion xscom_spci_regs;
} PnvPBCQState;

/*
 * We have one such address space wrapper per possible device under
 * the PHB since they need to be assigned statically at qemu device
 * creation time. The relationship to a PE is done later dynamically.
 * This means we can potentially create a lot of these guys. Q35
 * stores them as some kind of radix tree but we never really need to
 * do fast lookups so instead we simply keep a QLIST of them for now,
 * we can add the radix if needed later on.
 *
 * We do cache the PE number to speed things up a bit though.
 */
typedef struct PnvPhb3DMASpace {
    PCIBus *bus;
    uint8_t devfn;
    int pe_num;         /* Cached PE number */
#define PHB_INVALID_PE (-1)
    PnvPHB *phb;
    AddressSpace dma_as;
    IOMMUMemoryRegion dma_mr;
    MemoryRegion msi32_mr;
    MemoryRegion msi64_mr;
    QLIST_ENTRY(PnvPhb3DMASpace) list;
} PnvPhb3DMASpace;

/* pnv_phb4.h types */
#define PNV_PHB4_MAX_LSIs          8
#define PNV_PHB4_MAX_INTs          4096
#define PNV_PHB4_MAX_MIST          (PNV_PHB4_MAX_INTs >> 2)
#define PNV_PHB4_MAX_MMIO_WINDOWS  32
#define PNV_PHB4_MIN_MMIO_WINDOWS  16
#define PNV_PHB4_NUM_REGS          (0x3000 >> 3)
#define PNV_PHB4_MAX_PEs           512
#define PNV_PHB4_MAX_TVEs          (PNV_PHB4_MAX_PEs * 2)
#define PNV_PHB4_MAX_PEEVs         (PNV_PHB4_MAX_PEs / 64)
#define PNV_PHB4_MAX_MBEs          (PNV_PHB4_MAX_MMIO_WINDOWS * 2)
typedef struct PnvPhb4PecState PnvPhb4PecState;

/*
 * Unified PHB PCIe Host Bridge for PowerNV machines
 */
#define TYPE_PNV_PHB "pnv-phb"
OBJECT_DECLARE_SIMPLE_TYPE(PnvPHB, PNV_PHB)

#define PHB_VERSION_3    3
#define PHB_VERSION_4    4
#define PHB_VERSION_5    5

struct PnvPHB {
    PCIExpressHost parent_obj;

    uint64_t version;
    uint32_t chip_id;
    uint32_t phb_id;
    char bus_path[8];
    MemoryRegion pci_mmio;
    MemoryRegion pci_io;
    qemu_irq *qirqs;

    /*
     * PnvPHB3 attributes
     */
    uint64_t regs3[PNV_PHB3_NUM_REGS];
    MemoryRegion mr_regs3;

    MemoryRegion mr_m32;
    MemoryRegion mr_m64[PNV_PHB3_NUM_M64];

    uint64_t ioda2_LIST[8];
    uint64_t ioda2_LXIVT[8];
    uint64_t ioda2_TVT[512];
    uint64_t ioda2_M64BT[16];
    uint64_t ioda2_MDT[256];
    uint64_t ioda2_PEEV[4];

    uint32_t total_irq;
    ICSState lsis;
    Phb3MsiState msis;

    PnvPBCQState pbcq;

    QLIST_HEAD(, PnvPhb3DMASpace) v3_dma_spaces;

    PnvChip *chip;

    /*
     * PnvPHB4 attributes
     */
    /* The owner PEC */
    PnvPhb4PecState *pec;

    /* Main register images */
    uint64_t regs[PNV_PHB4_NUM_REGS];
    MemoryRegion mr_regs;

    /* Extra SCOM-only register */
    uint64_t scom_hv_ind_addr_reg;

    /*
     * Geometry of the PHB. There are two types, small and big PHBs, a
     * number of resources (number of PEs, windows etc...) are doubled
     * for a big PHB
     */
    bool big_phb;

    /* Memory regions for MMIO space */
    MemoryRegion mr_mmio[PNV_PHB4_MAX_MMIO_WINDOWS];

    /* PCI registers (excluding pass-through) */
#define PHB4_PEC_PCI_STK_REGS_COUNT  0xf
    uint64_t pci_regs[PHB4_PEC_PCI_STK_REGS_COUNT];
    MemoryRegion pci_regs_mr;

    /* Nest registers */
#define PHB4_PEC_NEST_STK_REGS_COUNT  0x17
    uint64_t nest_regs[PHB4_PEC_NEST_STK_REGS_COUNT];
    MemoryRegion nest_regs_mr;

    /* PHB pass-through XSCOM */
    MemoryRegion phb_regs_mr;

    /* Memory windows from PowerBus to PHB */
    MemoryRegion phbbar;
    MemoryRegion intbar;
    MemoryRegion mmbar0;
    MemoryRegion mmbar1;
    uint64_t mmio0_base;
    uint64_t mmio0_size;
    uint64_t mmio1_base;
    uint64_t mmio1_size;

    /* On-chip IODA tables */
    uint64_t ioda_LIST[PNV_PHB4_MAX_LSIs];
    uint64_t ioda_MIST[PNV_PHB4_MAX_MIST];
    uint64_t ioda_TVT[PNV_PHB4_MAX_TVEs];
    uint64_t ioda_MBT[PNV_PHB4_MAX_MBEs];
    uint64_t ioda_MDT[PNV_PHB4_MAX_PEs];
    uint64_t ioda_PEEV[PNV_PHB4_MAX_PEEVs];

    /*
     * The internal PESTA/B is 2 bits per PE split into two tables, we
     * store them in a single array here to avoid wasting space.
     */
    uint8_t  ioda_PEST_AB[PNV_PHB4_MAX_PEs];

    /* P9 Interrupt generation */
    XiveSource xsrc;

    QLIST_HEAD(, PnvPhb4DMASpace) dma_spaces;
};

/*
 * PHB PCIe Root port
 */
typedef struct PnvPHBRootPort {
    PCIESlot parent_obj;
} PnvPHBRootPort;

#define TYPE_PNV_PHB_ROOT_PORT "pnv-phb-root-port"
#define PNV_PHB_ROOT_PORT(obj) \
    OBJECT_CHECK(PnvPHBRootPort, obj, TYPE_PNV_PHB_ROOT_PORT)

#endif /* PCI_HOST_PNV_PHB_H */
