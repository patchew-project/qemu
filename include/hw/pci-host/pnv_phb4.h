/*
 * QEMU PowerPC PowerNV (POWER9) PHB4 model
 *
 * Copyright (c) 2018-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PCI_HOST_PNV_PHB4_H
#define PCI_HOST_PNV_PHB4_H

#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci-host/pnv_phb.h"
#include "hw/ppc/xive.h"
#include "qom/object.h"

typedef struct PnvPhb4PecState PnvPhb4PecState;
typedef struct PnvPhb4PecStack PnvPhb4PecStack;
typedef struct PnvChip PnvChip;

/*
 * We have one such address space wrapper per possible device under
 * the PHB since they need to be assigned statically at qemu device
 * creation time. The relationship to a PE is done later
 * dynamically. This means we can potentially create a lot of these
 * guys. Q35 stores them as some kind of radix tree but we never
 * really need to do fast lookups so instead we simply keep a QLIST of
 * them for now, we can add the radix if needed later on.
 *
 * We do cache the PE number to speed things up a bit though.
 */
typedef struct PnvPhb4DMASpace {
    PCIBus *bus;
    uint8_t devfn;
    int pe_num;         /* Cached PE number */
#define PHB_INVALID_PE (-1)
    PnvPHB *phb;
    AddressSpace dma_as;
    IOMMUMemoryRegion dma_mr;
    MemoryRegion msi32_mr;
    MemoryRegion msi64_mr;
    QLIST_ENTRY(PnvPhb4DMASpace) list;
} PnvPhb4DMASpace;

/*
 * PHB4 PCIe Root port
 */
#define TYPE_PNV_PHB4_ROOT_BUS "pnv-phb4-root"
#define TYPE_PNV_PHB4_ROOT_PORT "pnv-phb4-root-port"
#define TYPE_PNV_PHB5_ROOT_PORT "pnv-phb5-root-port"

typedef struct PnvPHB4RootPort {
    PCIESlot parent_obj;
} PnvPHB4RootPort;

struct PnvPHB4DeviceClass {
    DeviceClass parent_class;
};

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

#define PNV_PHB4_VERSION           0x000000a400000002ull
#define PNV_PHB4_DEVICE_ID         0x04c1

#define PCI_MMIO_TOTAL_SIZE        (0x1ull << 60)

void pnv_phb4_pic_print_info(PnvPHB *phb, Monitor *mon);
int pnv_phb4_pec_get_phb_id(PnvPhb4PecState *pec, int stack_index);
extern const MemoryRegionOps pnv_phb4_xscom_ops;

/*
 * PHB4 PEC (PCI Express Controller)
 */
#define TYPE_PNV_PHB4_PEC "pnv-phb4-pec"
OBJECT_DECLARE_TYPE(PnvPhb4PecState, PnvPhb4PecClass, PNV_PHB4_PEC)

struct PnvPhb4PecState {
    DeviceState parent;

    /* PEC number in chip */
    uint32_t index;
    uint32_t chip_id;

    MemoryRegion *system_memory;

    /* Nest registers, excuding per-stack */
#define PHB4_PEC_NEST_REGS_COUNT    0xf
    uint64_t nest_regs[PHB4_PEC_NEST_REGS_COUNT];
    MemoryRegion nest_regs_mr;

    /* PCI registers, excluding per-stack */
#define PHB4_PEC_PCI_REGS_COUNT     0x3
    uint64_t pci_regs[PHB4_PEC_PCI_REGS_COUNT];
    MemoryRegion pci_regs_mr;

    /* PHBs */
    uint32_t num_phbs;

    PnvChip *chip;
};


struct PnvPhb4PecClass {
    DeviceClass parent_class;

    uint32_t (*xscom_nest_base)(PnvPhb4PecState *pec);
    uint32_t xscom_nest_size;
    uint32_t (*xscom_pci_base)(PnvPhb4PecState *pec);
    uint32_t xscom_pci_size;
    const char *compat;
    int compat_size;
    const char *stk_compat;
    int stk_compat_size;
    uint64_t version;
    const char *phb_type;
    const uint32_t *num_phbs;
    const char *rp_model;
};

/*
 * POWER10 definitions
 */

#define TYPE_PNV_PHB5 "pnv-phb5"
#define PNV_PHB5(obj) \
    OBJECT_CHECK(PnvPhb, (obj), TYPE_PNV_PHB5)

#define PNV_PHB5_VERSION           0x000000a500000001ull
#define PNV_PHB5_DEVICE_ID         0x0652

#define TYPE_PNV_PHB5_PEC "pnv-phb5-pec"
#define PNV_PHB5_PEC(obj) \
    OBJECT_CHECK(PnvPhb4PecState, (obj), TYPE_PNV_PHB5_PEC)

void pnv_phb4_instance_init(Object *obj);
void pnv_phb4_realize(DeviceState *dev, Error **errp);
void pnv_phb4_xive_notify(XiveNotifier *xf, uint32_t srcno, bool pq_checked);

#endif /* PCI_HOST_PNV_PHB4_H */
