#ifndef HW_FDC_H
#define HW_FDC_H

#include "qapi/qapi-types-block.h"
#include "hw/sysbus.h"

/* fdc.c */
#define MAX_FD 2

#define TYPE_ISA_FDC "isa-fdc"

void isa_fdc_init_drives(ISADevice *fdc, DriveInfo **fds);
void sysbus_fdc_init_drives(SysBusDevice *dev, DriveInfo **fds);

FloppyDriveType isa_fdc_get_drive_type(ISADevice *fdc, int i);
int cmos_get_fd_drive_type(FloppyDriveType fd0);

#endif
