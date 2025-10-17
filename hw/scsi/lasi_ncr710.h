/*
 * LASI NCR53C710 SCSI Host Adapter
 */

#ifndef HW_LASI_NCR710_H
#define HW_LASI_NCR710_H

#include "hw/sysbus.h"
#include "qemu/osdep.h"
#include "exec/memattrs.h"
#include "hw/scsi/scsi.h"
#include "hw/scsi/ncr53c710.h"

#define TYPE_LASI_NCR710 "lasi-ncr710"
OBJECT_DECLARE_SIMPLE_TYPE(LasiNCR710State, LASI_NCR710)

/* LASI-specific constants */
#define LASI_SCSI_RESET         0x000   /* SCSI Reset Register */
#define LASI_SCSI_NCR710_BASE   0x100   /* NCR53C710 registers start here */

/* PA-RISC device identification register offsets */
#define PARISC_DEVICE_ID_OFF    0x00    /* HW type, HVERSION, SVERSION */
#define PARISC_DEVICE_CONFIG_OFF 0x04   /* Configuration data */

/* NCR710 register constants needed by LASI wrapper */
#define PHASE_MASK              7       /* Mask for phase bits */
#define PHASE_DO                0       /* Data out phase */

/* NCR710 register bit definitions needed by LASI wrapper */
#define NCR710_SCNTL1_RST       0x08    /* SCSI Reset */
#define NCR710_ISTAT_RST        0x40    /* Device Reset */
#define NCR710_ISTAT_ABRT       0x80    /* Script Abort */
#define NCR710_ISTAT_CON        0x08    /* Connected */
#define NCR710_DSTAT_DFE        0x80    /* DMA FIFO Empty */
#define NCR710_CTEST2_DACK      0x01    /* DMA Acknowledge */

/* LASI NCR53C710 state */
typedef struct LasiNCR710State {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq lasi_irq;       /* IRQ line to LASI controller */
    uint32_t hw_type;        /* Hardware type (HPHW_*) */
    uint32_t sversion;       /* Software version */
    uint32_t hversion;       /* Hardware version */
    SCSIBus bus;
    NCR710State ncr710;
} LasiNCR710State;

/* Create and initialize a LASI NCR710 device */
DeviceState *lasi_ncr710_init(MemoryRegion *addr_space, hwaddr hpa, qemu_irq irq);
void lasi_ncr710_handle_legacy_cmdline(DeviceState *lasi_dev);

#endif
