#ifndef HW_SOUTHBRIDGE_ICH9_SPI_H
#define HW_SOUTHBRIDGE_ICH9_SPI_H

#include "hw/acpi/ich9.h"
#include "hw/sysbus.h"
#include "exec/memory.h"
#include "qemu/notify.h"
#include "qom/object.h"

#define ICH9_SPI_SIZE 0x200 /* 512 bytes SPI configuration registers */

#define TYPE_ICH9_SPI_DEVICE "ICH9-SPI"
OBJECT_DECLARE_SIMPLE_TYPE(ICH9SPIState, ICH9_SPI_DEVICE)

extern const VMStateDescription vmstate_ich9_spi;

struct ICH9SPIState {
    /* <private> */
    SysBusDevice parent_obj;

     /* <public> */
    MemoryRegion mmio;
    MemoryRegion bios;
    MemoryRegion isa_bios;

    qemu_irq cs_line;
    SSIBus *spi;
    uint8_t regs[ICH9_SPI_SIZE];
};

void ich9_spi_init(PCIDevice *lpc_pci, ICH9SPIState *s, MemoryRegion *rcrb_mem);

/* ICH9: Chipset Configuration Registers Offset 3800h */

#define ICH9_SPI_BFPREG             0x00

#define ICH9_SPI_HSFS               0x04
#define ICH9_SPI_HSFS_FLOCKDN       BIT(15)

#define ICH9_SPI_FADDR              0x08
#define ICH9_SPI_FDATA0             0x10
#define ICH9_SPI_FDATA16            0x4C

#define ICH9_SPI_PR0                0x78
#define ICH9_SPI_PR4                0x84
#define ICH9_SPI_PR_WR_PROT         BIT(31)
#define ICH9_SPI_PR_RD_PROT         BIT(15)
#define ICH9_SPI_PR_LIMIT(x)        (((x) >> 4) | 0xfff)
#define ICH9_SPI_PR_BASE(x)         (((x) & 0x1fff) << 12)

#define ICH9_SPI_SSFS_FC            0x90
#define ICH9_SPI_SSFS_FC_FREQ_SHIFT 24
#define ICH9_SPI_SSFS_FC_FREQ_MASK  (0x3 << SSFS_FC_FREQ_SHIFT)
#define ICH9_SPI_SSFS_FC_SME        BIT(23)
#define ICH9_SPI_SSFS_FC_DS         BIT(22)
#define ICH9_SPI_SSFS_FC_DBC_SHIFT  16
#define ICH9_SPI_SSFS_FC_DBC_MASK   0x3f
#define ICH9_SPI_SSFS_FC_DBC(x)     (((x) >> ICH9_SPI_SSFS_FC_DBC_SHIFT) & \
                                     ICH9_SPI_SSFS_FC_DBC_MASK)
#define ICH9_SPI_SSFS_FC_COP_SHIFT  12
#define ICH9_SPI_SSFS_FC_COP_MASK   0x7
#define ICH9_SPI_SSFS_FC_COP(x)     (((x) >> ICH9_SPI_SSFS_FC_COP_SHIFT) & \
                                     ICH9_SPI_SSFS_FC_COP_MASK)
#define ICH9_SPI_SSFS_FC_SPOP       BIT(11)
#define ICH9_SPI_SSFS_FC_ACS        BIT(10)
#define ICH9_SPI_SSFS_FC_SCGO       BIT(9)
#define ICH9_SPI_SSFS_FC_AEL        BIT(4)
#define ICH9_SPI_SSFS_FC_FCERR      BIT(3)
#define ICH9_SPI_SSFS_FC_CDONE      BIT(2)
#define ICH9_SPI_SSFS_FC_SCIP       BIT(0)

#define ICH9_SPI_PREOP              0x94
#define ICH9_SPI_OPTYPE             0x96
#define ICH9_SPI_TYPE_WRITE         BIT(0)
#define ICH9_SPI_TYPE_ADDRESS_REQ   BIT(1)

#define ICH9_SPI_OPMENU             0x98
#define ICH9_SPI_OPMENU2            0x9C

#endif
