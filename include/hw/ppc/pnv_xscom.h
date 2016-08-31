#ifndef _HW_XSCOM_H
#define _HW_XSCOM_H
/*
 * QEMU PowerNV XSCOM bus definitions
 *
 * Copyright (c) 2010 David Gibson <david@gibson.dropbear.id.au>, IBM Corp.
 * Based on the s390 virtio bus definitions:
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <hw/ppc/pnv.h>

#define TYPE_XSCOM_DEVICE "xscom-device"
#define XSCOM_DEVICE(obj) \
     OBJECT_CHECK(XScomDevice, (obj), TYPE_XSCOM_DEVICE)
#define XSCOM_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(XScomDeviceClass, (klass), TYPE_XSCOM_DEVICE)
#define XSCOM_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XScomDeviceClass, (obj), TYPE_XSCOM_DEVICE)

#define TYPE_XSCOM_BUS "xscom-bus"
#define XSCOM_BUS(obj) OBJECT_CHECK(XScomBus, (obj), TYPE_XSCOM_BUS)

typedef struct XScomDevice XScomDevice;
typedef struct XScomBus XScomBus;

typedef struct XScomDeviceClass {
    DeviceClass parent_class;

    const char *dt_name;
    const char **dt_compatible;
    int (*init)(XScomDevice *dev);
    int (*devnode)(XScomDevice *dev, void *fdt, int offset);

    /* Actual XScom accesses */
    bool (*read)(XScomDevice *dev, uint32_t range, uint32_t offset,
                 uint64_t *out_val);
    bool (*write)(XScomDevice *dev, uint32_t range, uint32_t offset,
                  uint64_t val);
} XScomDeviceClass;

typedef struct XScomRange {
    uint32_t addr;
    uint32_t size;
} XScomRange;

struct XScomDevice {
    DeviceState qdev;
#define MAX_XSCOM_RANGES 4
    struct XScomRange ranges[MAX_XSCOM_RANGES];
};

struct XScomBus {
    BusState bus;
    uint32_t chip_id;
};

extern XScomBus *xscom_create(PnvChip *chip);
extern int xscom_populate_fdt(XScomBus *xscom, void *fdt, int offset);


#endif /* _HW_XSCOM_H */
