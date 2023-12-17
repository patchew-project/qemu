#ifndef HW_FDC_ISA_H
#define HW_FDC_ISA_H

#include "exec/hwaddr.h"
#include "qapi/qapi-types-block.h"
#include "hw/block/fdc.h"
#include "hw/isa/isa.h"

#define TYPE_ISA_FDC "isa-fdc"

OBJECT_DECLARE_SIMPLE_TYPE(FDCtrlISABus, ISA_FDC)

struct FDCtrlISABus {
    ISADevice parent_obj;

    uint32_t iobase;
    uint32_t irq;
    uint32_t dma;
    FDCtrl state;
    int32_t bootindexA;
    int32_t bootindexB;
};

void isa_fdc_init_drives(ISADevice *fdc, DriveInfo **fds);
void fdctrl_init_sysbus(qemu_irq irq, hwaddr mmio_base, DriveInfo **fds);
void sun4m_fdctrl_init(qemu_irq irq, hwaddr io_base,
                       DriveInfo **fds, qemu_irq *fdc_tc);

FloppyDriveType isa_fdc_get_drive_type(ISADevice *fdc, int i);
int cmos_get_fd_drive_type(FloppyDriveType fd0);

#endif
