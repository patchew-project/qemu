/*
 * QEMU PowerPC PowerNV machine model
 *
 * Native version of ICS/ICP
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
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "qemu/log.h"
#include "qapi/error.h"

#include "hw/ppc/fdt.h"
#include "hw/ppc/xics.h"
#include "hw/ppc/pnv.h"

#include <libfdt.h>

static void xics_native_reset(void *opaque)
{
    device_reset(DEVICE(opaque));
}

static void xics_native_initfn(Object *obj)
{
    XICSState *xics = XICS_COMMON(obj);

    QLIST_INIT(&xics->ics);

    /*
     * Let's not forget to register a reset handler else the ICPs
     * won't be initialized with the correct values. Trouble ahead !
     */
    qemu_register_reset(xics_native_reset, xics);
}

static uint64_t xics_native_read(void *opaque, hwaddr addr, unsigned width)
{
    XICSState *s = opaque;
    uint32_t cpu_id = (addr & (PNV_XICS_SIZE - 1)) >> 12;
    bool byte0 = (width == 1 && (addr & 0x3) == 0);
    uint64_t val = 0xffffffff;
    ICPState *ss;

    ss = xics_find_icp(s, cpu_id);
    if (!ss) {
        qemu_log_mask(LOG_GUEST_ERROR, "XICS: Bad ICP server %d\n", cpu_id);
        return val;
    }

    switch (addr & 0xffc) {
    case 0: /* poll */
        val = icp_ipoll(ss, NULL);
        if (byte0) {
            val >>= 24;
        } else if (width != 4) {
            goto bad_access;
        }
        break;
    case 4: /* xirr */
        if (byte0) {
            val = icp_ipoll(ss, NULL) >> 24;
        } else if (width == 4) {
            val = icp_accept(ss);
        } else {
            goto bad_access;
        }
        break;
    case 12:
        if (byte0) {
            val = ss->mfrr;
        } else {
            goto bad_access;
        }
        break;
    case 16:
        if (width == 4) {
            val = ss->links[0];
        } else {
            goto bad_access;
        }
        break;
    case 20:
        if (width == 4) {
            val = ss->links[1];
        } else {
            goto bad_access;
        }
        break;
    case 24:
        if (width == 4) {
            val = ss->links[2];
        } else {
            goto bad_access;
        }
        break;
    default:
bad_access:
        qemu_log_mask(LOG_GUEST_ERROR, "XICS: Bad ICP access 0x%"
                      HWADDR_PRIx"/%d\n", addr, width);
    }

    return val;
}

static void xics_native_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned width)
{
    XICSState *s = opaque;
    uint32_t cpu_id = (addr & (PNV_XICS_SIZE - 1)) >> 12;
    bool byte0 = (width == 1 && (addr & 0x3) == 0);
    ICPState *ss;

    ss = xics_find_icp(s, cpu_id);
    if (!ss) {
        qemu_log_mask(LOG_GUEST_ERROR, "XICS: Bad ICP server %d\n", cpu_id);
        return;
    }

    switch (addr & 0xffc) {
    case 4: /* xirr */
        if (byte0) {
            icp_set_cppr(s, cpu_id, val);
        } else if (width == 4) {
            icp_eoi(s, cpu_id, val);
        } else {
            goto bad_access;
        }
        break;
    case 12:
        if (byte0) {
            icp_set_mfrr(s, cpu_id, val);
        } else {
            goto bad_access;
        }
        break;
    case 16:
        if (width == 4) {
            ss->links[0] = val;
        } else {
            goto bad_access;
        }
        break;
    case 20:
        if (width == 4) {
            ss->links[1] = val;
        } else {
            goto bad_access;
        }
        break;
    case 24:
        if (width == 4) {
            ss->links[2] = val;
        } else {
            goto bad_access;
        }
        break;
    default:
bad_access:
        qemu_log_mask(LOG_GUEST_ERROR, "XICS: Bad ICP access 0x%"
                      HWADDR_PRIx"/%d\n", addr, width);
    }
}

static const MemoryRegionOps xics_native_ops = {
    .read = xics_native_read,
    .write = xics_native_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void xics_set_nr_servers(XICSState *icp, uint32_t nr_servers,
                                Error **errp)
{
    int i;

    icp->nr_servers = nr_servers;

    icp->ss = g_malloc0(icp->nr_servers * sizeof(ICPState));
    for (i = 0; i < icp->nr_servers; i++) {
        char name[32];
        object_initialize(&icp->ss[i], sizeof(icp->ss[i]), TYPE_ICP);
        snprintf(name, sizeof(name), "icp[%d]", i);
        object_property_add_child(OBJECT(icp), name, OBJECT(&icp->ss[i]),
                                  errp);
    }
}

static void xics_native_realize(DeviceState *dev, Error **errp)
{
    XICSState *xics = XICS_COMMON(dev);
    XICSNative *xicsn = XICS_NATIVE(dev);
    Error *error = NULL;
    int i;

    if (!xics->nr_servers) {
        error_setg(errp, "Number of servers needs to be greater than 0");
        return;
    }

    for (i = 0; i < xics->nr_servers; i++) {
        object_property_set_bool(OBJECT(&xics->ss[i]), true, "realized",
                                 &error);
        if (error) {
            error_propagate(errp, error);
            return;
        }
    }

    xicsn->pir_table = g_hash_table_new(g_direct_hash, g_direct_equal);

    /* Register MMIO regions */
    memory_region_init_io(&xicsn->icp_mmio, OBJECT(dev), &xics_native_ops,
                          xicsn, "xics", PNV_XICS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xicsn->icp_mmio);
}

static void xics_native_cpu_setup(XICSState *xics, PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    XICSNative *xicsn = XICS_NATIVE(xics);

    assert(cs->cpu_index < xics->nr_servers);
    g_hash_table_insert(xicsn->pir_table, GINT_TO_POINTER(env->spr[SPR_PIR]),
                        (gpointer) &xics->ss[cs->cpu_index]);
}

static ICPState *xics_native_find_icp(XICSState *xics, int pir)
{
    XICSNative *xicsn = XICS_NATIVE(xics);
    gpointer key, value;
    gboolean found = g_hash_table_lookup_extended(xicsn->pir_table,
                                GINT_TO_POINTER(pir), &key, &value);

    assert(found);

    return (ICPState *) value;
}

static void xics_native_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    XICSStateClass *xsc = XICS_NATIVE_CLASS(oc);

    dc->realize = xics_native_realize;
    xsc->set_nr_servers = xics_set_nr_servers;
    xsc->cpu_setup = xics_native_cpu_setup;
    xsc->find_icp = xics_native_find_icp;
}

static const TypeInfo xics_native_info = {
    .name          = TYPE_XICS_NATIVE,
    .parent        = TYPE_XICS_COMMON,
    .instance_size = sizeof(XICSNative),
    .class_size = sizeof(XICSStateClass),
    .class_init    = xics_native_class_init,
    .instance_init = xics_native_initfn,
};

static void xics_native_register_types(void)
{
    type_register_static(&xics_native_info);
}

type_init(xics_native_register_types)

void xics_native_populate_icp(PnvChip *chip, void *fdt, int offset,
                              uint32_t pir, uint32_t count)
{
    uint64_t addr;
    char *name;
    const char compat[] = "IBM,power8-icp\0IBM,ppc-xicp";
    uint32_t irange[2], i, rsize;
    uint64_t *reg;

    /*
     * TODO: add multichip ICP BAR
     */
    addr = PNV_XICS_BASE | (pir << 12);

    irange[0] = cpu_to_be32(pir);
    irange[1] = cpu_to_be32(count);

    rsize = sizeof(uint64_t) * 2 * count;
    reg = g_malloc(rsize);
    for (i = 0; i < count; i++) {
        reg[i * 2] = cpu_to_be64(addr | ((pir + i) * 0x1000));
        reg[i * 2 + 1] = cpu_to_be64(0x1000);
    }

    name = g_strdup_printf("interrupt-controller@%"PRIX64, addr);
    offset = fdt_add_subnode(fdt, offset, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "compatible", compat, sizeof(compat))));
    _FDT((fdt_setprop(fdt, offset, "reg", reg, rsize)));
    _FDT((fdt_setprop_string(fdt, offset, "device_type",
                              "PowerPC-External-Interrupt-Presentation")));
    _FDT((fdt_setprop(fdt, offset, "interrupt-controller", NULL, 0)));
    _FDT((fdt_setprop(fdt, offset, "ibm,interrupt-server-ranges",
                       irange, sizeof(irange))));
    _FDT((fdt_setprop_cell(fdt, offset, "#interrupt-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 0)));
    g_free(reg);
}
