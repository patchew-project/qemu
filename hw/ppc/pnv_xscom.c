
/*
 * QEMU PowerNV XSCOM bus definitions
 *
 * Copyright (c) 2010 David Gibson, IBM Corporation <dwg@au1.ibm.com>
 * Based on the s390 virtio bus code:
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

/* TODO: Add some infrastructure for "random stuff" and FIRs that
 * various units might want to deal with without creating actual
 * XSCOM devices.
 *
 * For example, HB LPC XSCOM in the PIBAM
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "monitor/monitor.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "sysemu/device_tree.h"
#include "hw/ppc/fdt.h"

#include "hw/ppc/pnv_xscom.h"

#include <libfdt.h>

#define TYPE_XSCOM "xscom"
#define XSCOM(obj) OBJECT_CHECK(XScomState, (obj), TYPE_XSCOM)

#define XSCOM_SIZE        0x800000000ull
#define XSCOM_BASE(chip)  (0x3fc0000000000ull + ((uint64_t)(chip)) * XSCOM_SIZE)


typedef struct XScomState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    int32_t chip_id;
    PnvChipClass *chip_class;
    XScomBus *bus;
} XScomState;

static uint32_t xscom_to_pcb_addr(uint64_t addr)
{
        addr &= (XSCOM_SIZE - 1);
        return ((addr >> 4) & ~0xfull) | ((addr >> 3) & 0xf);
}

static void xscom_complete(uint64_t hmer_bits)
{
    CPUState *cs = current_cpu;
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    cpu_synchronize_state(cs);
    env->spr[SPR_HMER] |= hmer_bits;

    /* XXX Need a CPU helper to set HMER, also handle gneeration
     * of HMIs
     */
}

static XScomDevice *xscom_find_target(XScomState *s, uint32_t pcb_addr,
                                      uint32_t *range)
{
    BusChild *bc;

    QTAILQ_FOREACH(bc, &s->bus->bus.children, sibling) {
        DeviceState *qd = bc->child;
        XScomDevice *xd = XSCOM_DEVICE(qd);
        unsigned int i;

        for (i = 0; i < MAX_XSCOM_RANGES; i++) {
            if (xd->ranges[i].addr <= pcb_addr &&
                (xd->ranges[i].addr + xd->ranges[i].size) > pcb_addr) {
                *range = i;
                return xd;
            }
        }
    }
    return NULL;
}

static bool xscom_dispatch_read(XScomState *s, uint32_t pcb_addr,
                                uint64_t *out_val)
{
    uint32_t range, offset;
    struct XScomDevice *xd = xscom_find_target(s, pcb_addr, &range);
    XScomDeviceClass *xc;

    if (!xd) {
        return false;
    }
    xc = XSCOM_DEVICE_GET_CLASS(xd);
    if (!xc->read) {
        return false;
    }
    offset = pcb_addr - xd->ranges[range].addr;
    return xc->read(xd, range, offset, out_val);
}

static bool xscom_dispatch_write(XScomState *s, uint32_t pcb_addr, uint64_t val)
{
    uint32_t range, offset;
    struct XScomDevice *xd = xscom_find_target(s, pcb_addr, &range);
    XScomDeviceClass *xc;

    if (!xd) {
        return false;
    }
    xc = XSCOM_DEVICE_GET_CLASS(xd);
    if (!xc->write) {
        return false;
    }
    offset = pcb_addr - xd->ranges[range].addr;
    return xc->write(xd, range, offset, val);
}

static uint64_t xscom_read(void *opaque, hwaddr addr, unsigned width)
{
    XScomState *s = opaque;
    uint32_t pcba = xscom_to_pcb_addr(addr);
    uint64_t val;

    assert(width == 8);

    /* Handle some SCOMs here before dispatch */
    switch (pcba) {
    case 0xf000f:
        val = s->chip_class->chip_f000f;
        break;
    case 0x1010c00:     /* PIBAM FIR */
    case 0x1010c03:     /* PIBAM FIR MASK */
    case 0x2020007:     /* ADU stuff */
    case 0x2020009:     /* ADU stuff */
    case 0x202000f:     /* ADU stuff */
        val = 0;
        break;
    case 0x2013f00:     /* PBA stuff */
    case 0x2013f01:     /* PBA stuff */
    case 0x2013f02:     /* PBA stuff */
    case 0x2013f03:     /* PBA stuff */
    case 0x2013f04:     /* PBA stuff */
    case 0x2013f05:     /* PBA stuff */
    case 0x2013f06:     /* PBA stuff */
    case 0x2013f07:     /* PBA stuff */
        val = 0;
        break;
    default:
        if (!xscom_dispatch_read(s, pcba, &val)) {
            xscom_complete(HMER_XSCOM_FAIL | HMER_XSCOM_DONE);
            return 0;
        }
    }

    xscom_complete(HMER_XSCOM_DONE);
    return val;
}

static void xscom_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned width)
{
    XScomState *s = opaque;
    uint32_t pcba = xscom_to_pcb_addr(addr);

    assert(width == 8);

    /* Handle some SCOMs here before dispatch */
    switch (pcba) {
        /* We ignore writes to these */
    case 0xf000f:       /* chip id is RO */
    case 0x1010c00:     /* PIBAM FIR */
    case 0x1010c01:     /* PIBAM FIR */
    case 0x1010c02:     /* PIBAM FIR */
    case 0x1010c03:     /* PIBAM FIR MASK */
    case 0x1010c04:     /* PIBAM FIR MASK */
    case 0x1010c05:     /* PIBAM FIR MASK */
    case 0x2020007:     /* ADU stuff */
    case 0x2020009:     /* ADU stuff */
    case 0x202000f:     /* ADU stuff */
        break;
    default:
        if (!xscom_dispatch_write(s, pcba, val)) {
            xscom_complete(HMER_XSCOM_FAIL | HMER_XSCOM_DONE);
            return;
        }
    }

    xscom_complete(HMER_XSCOM_DONE);
}

static const MemoryRegionOps xscom_ops = {
    .read = xscom_read,
    .write = xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static int xscom_init(SysBusDevice *dev)
{
    XScomState *s = XSCOM(dev);

    s->chip_id = -1;
    return 0;
}

static void xscom_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    XScomState *s = XSCOM(dev);
    char *name;

    if (s->chip_id < 0) {
        error_setg(errp, "invalid chip id '%d'", s->chip_id);
        return;
    }
    name = g_strdup_printf("xscom-%x", s->chip_id);
    memory_region_init_io(&s->mem, OBJECT(s), &xscom_ops, s, name, XSCOM_SIZE);
    sysbus_init_mmio(sbd, &s->mem);
    sysbus_mmio_map(sbd, 0, XSCOM_BASE(s->chip_id));
}

static Property xscom_properties[] = {
        DEFINE_PROP_INT32("chip_id", XScomState, chip_id, 0),
        DEFINE_PROP_END_OF_LIST(),
};

static void xscom_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = xscom_properties;
    dc->realize = xscom_realize;
    k->init = xscom_init;
}

static const TypeInfo xscom_info = {
    .name          = TYPE_XSCOM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XScomState),
    .class_init    = xscom_class_init,
};

static void xscom_bus_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo xscom_bus_info = {
    .name = TYPE_XSCOM_BUS,
    .parent = TYPE_BUS,
    .class_init = xscom_bus_class_init,
    .instance_size = sizeof(XScomBus),
};

XScomBus *xscom_create(PnvChip *chip)
{
    DeviceState *dev;
    XScomState *xdev;
    BusState *qbus;
    XScomBus *xb;
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);

    dev = qdev_create(NULL, TYPE_XSCOM);
    qdev_prop_set_uint32(dev, "chip_id", chip->chip_id);
    qdev_init_nofail(dev);

    /* Create bus on bridge device */
    qbus = qbus_create(TYPE_XSCOM_BUS, dev, "xscom");
    xb = DO_UPCAST(XScomBus, bus, qbus);
    xb->chip_id = chip->chip_id;
    xdev = XSCOM(dev);
    xdev->bus = xb;
    xdev->chip_class = pcc;

    return xb;
}

int xscom_populate_fdt(XScomBus *xb, void *fdt, int root_offset)
{
    BusChild *bc;
    char *name;
    const char compat[] = "ibm,power8-xscom\0ibm,xscom";
    uint64_t reg[] = { cpu_to_be64(XSCOM_BASE(xb->chip_id)),
                       cpu_to_be64(XSCOM_SIZE) };
    int xscom_offset;

    name = g_strdup_printf("xscom@%llx", (unsigned long long)
                           be64_to_cpu(reg[0]));
    xscom_offset = fdt_add_subnode(fdt, root_offset, name);
    _FDT(xscom_offset);
    g_free(name);
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "ibm,chip-id", xb->chip_id)));
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "#address-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "#size-cells", 1)));
    _FDT((fdt_setprop(fdt, xscom_offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop(fdt, xscom_offset, "compatible", compat,
                      sizeof(compat))));
    _FDT((fdt_setprop(fdt, xscom_offset, "scom-controller", NULL, 0)));

    QTAILQ_FOREACH(bc, &xb->bus.children, sibling) {
        DeviceState *qd = bc->child;
        XScomDevice *xd = XSCOM_DEVICE(qd);
        XScomDeviceClass *xc = XSCOM_DEVICE_GET_CLASS(xd);
        uint32_t reg[MAX_XSCOM_RANGES * 2];
        unsigned int i, sz = 0;
        void *cp, *p;
        int child_offset;

        /* Some XSCOM slaves may not be represented in the DT */
        if (!xc->dt_name) {
            continue;
        }
        name = g_strdup_printf("%s@%x", xc->dt_name, xd->ranges[0].addr);
        child_offset = fdt_add_subnode(fdt, xscom_offset, name);
        _FDT(child_offset);
        g_free(name);
        for (i = 0; i < MAX_XSCOM_RANGES; i++) {
            if (xd->ranges[i].size == 0) {
                break;
            }
            reg[sz++] = cpu_to_be32(xd->ranges[i].addr);
            reg[sz++] = cpu_to_be32(xd->ranges[i].size);
        }
        _FDT((fdt_setprop(fdt, child_offset, "reg", reg, sz * 4)));
        if (xc->devnode) {
            _FDT((xc->devnode(xd, fdt, child_offset)));
        }
#define MAX_COMPATIBLE_PROP     1024
        cp = p = g_malloc0(MAX_COMPATIBLE_PROP);
        i = 0;
        while ((p - cp) < MAX_COMPATIBLE_PROP) {
            int l;
            if (xc->dt_compatible[i] == NULL) {
                break;
            }
            l = strlen(xc->dt_compatible[i]);
            if (l >= (MAX_COMPATIBLE_PROP - i)) {
                break;
            }
            strcpy(p, xc->dt_compatible[i++]);
            p += l + 1;
        }
        _FDT((fdt_setprop(fdt, child_offset, "compatible", cp, p - cp)));
    }

    return 0;
}

static int xscom_qdev_init(DeviceState *qdev)
{
    XScomDevice *xdev = (XScomDevice *)qdev;
    XScomDeviceClass *xc = XSCOM_DEVICE_GET_CLASS(xdev);

    if (xc->init) {
        return xc->init(xdev);
    }
    return 0;
}

static void xscom_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->init = xscom_qdev_init;
    k->bus_type = TYPE_XSCOM_BUS;
}

static const TypeInfo xscom_dev_info = {
    .name = TYPE_XSCOM_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XScomDevice),
    .abstract = true,
    .class_size = sizeof(XScomDeviceClass),
    .class_init = xscom_device_class_init,
};

static void xscom_register_types(void)
{
    type_register_static(&xscom_info);
    type_register_static(&xscom_bus_info);
    type_register_static(&xscom_dev_info);
}

type_init(xscom_register_types)
