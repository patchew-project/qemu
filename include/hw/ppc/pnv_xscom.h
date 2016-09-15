/*
 * QEMU PowerPC PowerNV XSCOM bus definitions
 *
 * Copyright (c) 2016, IBM Corporation.
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
#ifndef _PPC_PNV_XSCOM_H
#define _PPC_PNV_XSCOM_H

#include "hw/sysbus.h"

typedef struct PnvXScomInterface {
    Object parent;
} PnvXScomInterface;

#define TYPE_PNV_XSCOM_INTERFACE "pnv-xscom-interface"
#define PNV_XSCOM_INTERFACE(obj) \
     OBJECT_CHECK(PnvXScomInterface, (obj), TYPE_PNV_XSCOM_INTERFACE)
#define PNV_XSCOM_INTERFACE_CLASS(klass)                \
    OBJECT_CLASS_CHECK(PnvXScomInterfaceClass, (klass), \
                       TYPE_PNV_XSCOM_INTERFACE)
#define PNV_XSCOM_INTERFACE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvXScomInterfaceClass, (obj), TYPE_PNV_XSCOM_INTERFACE)

typedef struct PnvXScomInterfaceClass {
    InterfaceClass parent;
    int (*devnode)(PnvXScomInterface *dev, void *fdt, int offset);

    uint64_t (*xscom_addr)(uint32_t pcba);
    uint32_t (*xscom_pcba)(uint64_t addr);
} PnvXScomInterfaceClass;

#define TYPE_PNV_XSCOM "pnv-xscom"
#define PNV_XSCOM(obj) OBJECT_CHECK(PnvXScom, (obj), TYPE_PNV_XSCOM)

typedef struct PnvChipClass PnvChipClass;

typedef struct PnvXScom {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    int32_t chip_id;
    PnvChipClass *chip_class;
    MemoryRegion xscom_mr;
    AddressSpace xscom_as;
} PnvXScom;

#define PNV_XSCOM_SIZE        0x800000000ull
#define PNV_XSCOM_BASE(chip)                                    \
    (0x3fc0000000000ull + ((uint64_t)(chip)) * PNV_XSCOM_SIZE)

extern int pnv_xscom_populate_fdt(PnvXScom *xscom, void *fdt, int offset);

/*
 * helpers to translate to XScomm PCB addresses
 */
extern uint32_t pnv_xscom_pcba(PnvXScomInterface *dev, uint64_t addr);
extern uint64_t pnv_xscom_addr(PnvXScomInterface *dev, uint32_t pcba);

#endif /* _PPC_PNV_XSCOM_H */
