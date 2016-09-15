/*
 * QEMU PowerPC PowerNV XSCOM bus
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
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "qemu/log.h"
#include "sysemu/kvm.h"
#include "target-ppc/cpu.h"
#include "hw/sysbus.h"

#include "hw/ppc/fdt.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv.h"

#include <libfdt.h>

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

static bool xscom_dispatch_read(PnvXScom *xscom, hwaddr addr, uint64_t *val)
{
    uint32_t success;
    uint8_t data[8];

    success = !address_space_rw(&xscom->xscom_as, addr, MEMTXATTRS_UNSPECIFIED,
                                data, 8, false);
    *val = (((uint64_t) data[0]) << 56 |
            ((uint64_t) data[1]) << 48 |
            ((uint64_t) data[2]) << 40 |
            ((uint64_t) data[3]) << 32 |
            ((uint64_t) data[4]) << 24 |
            ((uint64_t) data[5]) << 16 |
            ((uint64_t) data[6]) << 8  |
            ((uint64_t) data[7]));
    return success;
}

static bool xscom_dispatch_write(PnvXScom *xscom, hwaddr addr, uint64_t val)
{
    uint32_t success;
    uint8_t data[8];

    data[0] = (val >> 56) & 0xff;
    data[1] = (val >> 48) & 0xff;
    data[2] = (val >> 40) & 0xff;
    data[3] = (val >> 32) & 0xff;
    data[4] = (val >> 24) & 0xff;
    data[5] = (val >> 16) & 0xff;
    data[6] = (val >> 8) & 0xff;
    data[7] = val & 0xff;

    success = !address_space_rw(&xscom->xscom_as, addr, MEMTXATTRS_UNSPECIFIED,
                           data, 8, true);
    return success;
}

static uint64_t xscom_read(void *opaque, hwaddr addr, unsigned width)
{
    PnvXScom *s = opaque;
    uint32_t pcba = s->chip_class->xscom_pcba(addr);
    uint64_t val = 0;

    /* Handle some SCOMs here before dispatch */
    switch (pcba) {
    case 0xf000f:
        val = s->chip_class->chip_cfam_id;
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
        if (!xscom_dispatch_read(s, addr, &val)) {
            qemu_log_mask(LOG_GUEST_ERROR, "XSCOM read failed at @0x%"
                          HWADDR_PRIx " pcba=0x%08x\n", addr, pcba);
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
    PnvXScom *s = opaque;
    uint32_t pcba = s->chip_class->xscom_pcba(addr);

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
        if (!xscom_dispatch_write(s, addr, val)) {
            qemu_log_mask(LOG_GUEST_ERROR, "XSCOM write failed at @0x%"
                          HWADDR_PRIx " pcba=0x%08x data=0x%" PRIx64 "\n",
                          addr, pcba, val);
            xscom_complete(HMER_XSCOM_FAIL | HMER_XSCOM_DONE);
            return;
        }
    }

    xscom_complete(HMER_XSCOM_DONE);
}

const MemoryRegionOps pnv_xscom_ops = {
    .read = xscom_read,
    .write = xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_xscom_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    PnvXScom *s = PNV_XSCOM(dev);
    char *name;
    Object *obj;
    Error *err = NULL;

    obj = object_property_get_link(OBJECT(s), "chip", &err);
    if (!obj) {
        error_setg(errp, "%s: required link 'chip' not found: %s",
                     __func__, error_get_pretty(err));
        return;
    }

    s->chip_class = PNV_CHIP_GET_CLASS(obj);
    s->chip_id = PNV_CHIP(obj)->chip_id;

    if (s->chip_id < 0) {
        error_setg(errp, "invalid chip id '%d'", s->chip_id);
        return;
    }

    name = g_strdup_printf("xscom-%x", s->chip_id);
    memory_region_init_io(&s->mem, OBJECT(s), &pnv_xscom_ops, s, name,
                          PNV_XSCOM_SIZE);
    sysbus_init_mmio(sbd, &s->mem);

    memory_region_init(&s->xscom_mr, OBJECT(s), name, PNV_XSCOM_SIZE);
    address_space_init(&s->xscom_as, &s->xscom_mr, name);
    g_free(name);

}

static void pnv_xscom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_xscom_realize;
}

static const TypeInfo pnv_xscom_info = {
    .name          = TYPE_PNV_XSCOM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PnvXScom),
    .class_init    = pnv_xscom_class_init,
};

static const TypeInfo pnv_xscom_interface_info = {
    .name = TYPE_PNV_XSCOM_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(PnvXScomInterfaceClass),
};

static void pnv_xscom_register_types(void)
{
    type_register_static(&pnv_xscom_info);
    type_register_static(&pnv_xscom_interface_info);
}

type_init(pnv_xscom_register_types)

typedef struct ForeachPopulateArgs {
    void *fdt;
    int xscom_offset;
} ForeachPopulateArgs;

static int xscom_populate_child(Object *child, void *opaque)
{
    if (object_dynamic_cast(child, TYPE_PNV_XSCOM_INTERFACE)) {
        ForeachPopulateArgs *args = opaque;
        PnvXScomInterface *xd = PNV_XSCOM_INTERFACE(child);;
        PnvXScomInterfaceClass *xc = PNV_XSCOM_INTERFACE_GET_CLASS(xd);

        if (xc->devnode) {
            _FDT((xc->devnode(xd, args->fdt, args->xscom_offset)));
        }
    }
    return 0;
}

int pnv_xscom_populate_fdt(PnvXScom *adu, void *fdt, int root_offset)
{
    const char compat[] = "ibm,power8-xscom\0ibm,xscom";
    uint64_t reg[] = { cpu_to_be64(PNV_XSCOM_BASE(adu->chip_id)),
                       cpu_to_be64(PNV_XSCOM_SIZE) };
    int xscom_offset;
    ForeachPopulateArgs args;
    char *name;

    name = g_strdup_printf("xscom@%" PRIx64, be64_to_cpu(reg[0]));
    xscom_offset = fdt_add_subnode(fdt, root_offset, name);
    _FDT(xscom_offset);
    g_free(name);
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "ibm,chip-id", adu->chip_id)));
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "#address-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "#size-cells", 1)));
    _FDT((fdt_setprop(fdt, xscom_offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop(fdt, xscom_offset, "compatible", compat,
                      sizeof(compat))));
    _FDT((fdt_setprop(fdt, xscom_offset, "scom-controller", NULL, 0)));

    args.fdt = fdt;
    args.xscom_offset = xscom_offset;

    object_child_foreach(OBJECT(adu), xscom_populate_child, &args);
    return 0;
}

/*
 * XScom address translation depends on the chip type and not all
 * objects have backlink to it. Here's a helper to handle this case.
 * To be improved.
 */
uint32_t pnv_xscom_pcba(PnvXScomInterface *dev, uint64_t addr)
{
    PnvXScomInterfaceClass *xc = PNV_XSCOM_INTERFACE_GET_CLASS(dev);

    if (!xc->xscom_pcba) {
        PnvMachineState *pnv = POWERNV_MACHINE(qdev_get_machine());
        PnvChipClass *pcc = PNV_CHIP_GET_CLASS(pnv->chips[0]);

        xc->xscom_pcba = pcc->xscom_pcba;
    }

    return xc->xscom_pcba(addr);
}

uint64_t pnv_xscom_addr(PnvXScomInterface *dev, uint32_t pcba)
{
     PnvXScomInterfaceClass *xc = PNV_XSCOM_INTERFACE_GET_CLASS(dev);

    if (!xc->xscom_addr) {
        PnvMachineState *pnv = POWERNV_MACHINE(qdev_get_machine());
        PnvChipClass *pcc = PNV_CHIP_GET_CLASS(pnv->chips[0]);

        xc->xscom_addr = pcc->xscom_addr;
    }

    return xc->xscom_addr(pcba);
}
