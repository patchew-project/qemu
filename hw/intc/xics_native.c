/*
 * QEMU PowerPC hardware System Emulator
 *
 * Native version of ICS/ICP
 *
 * Copyright (c) 2010,2011 David Gibson, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "trace.h"
#include "qemu/timer.h"
#include "hw/ppc/xics.h"
#include "qapi/visitor.h"
#include "qapi/error.h"

#include <libfdt.h>

/* #define DEBUG_MM(fmt...)      printf(fmt) */
#define DEBUG_MM(fmt...)        do { } while (0)

static void xics_native_initfn(Object *obj)
{
    XICSState *xics = XICS_NATIVE(obj);

    QLIST_INIT(&xics->ics);
}

static uint64_t icp_mm_read(void *opaque, hwaddr addr, unsigned width)
{
    XICSState *s = opaque;
    int32_t cpu_id, server;
    uint32_t val;
    ICPState *ss;
    bool byte0 = (width == 1 && (addr & 0x3) == 0);

    cpu_id = (addr & (ICP_MM_SIZE - 1)) >> 12;
    server = get_cpu_index_by_dt_id(cpu_id);
    if (server < 0) {
        fprintf(stderr, "XICS: Bad ICP server %d\n", server);
        goto bad_access;
    }
    ss = &s->ss[server];

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
        fprintf(stderr, "XICS: Bad ICP access %llx/%d\n",
                (unsigned long long)addr, width);
        val = 0xffffffff;
    }
    DEBUG_MM("icp_mm_read(addr=%016llx,serv=0x%x/%d,off=%d,w=%d,val=0x%08x)\n",
             (unsigned long long)addr, cpu_id, server, (int)(addr & 0xffc),
             width, val);

    return val;
}

static void icp_mm_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned width)
{
    XICSState *s = opaque;
    int32_t cpu_id, server;
    ICPState *ss;
    bool byte0 = (width == 1 && (addr & 0x3) == 0);

    cpu_id = (addr & (ICP_MM_SIZE - 1)) >> 12;
    server = get_cpu_index_by_dt_id(cpu_id);
    if (server < 0) {
        fprintf(stderr, "XICS: Bad ICP server %d\n", server);
        goto bad_access;
    }
    ss = &s->ss[server];

    DEBUG_MM("icp_mm_write(addr=%016llx,serv=0x%x/%d,off=%d,w=%d,val=0x%08x)\n",
             (unsigned long long)addr, cpu_id, server,
             (int)(addr & 0xffc), width, (uint32_t)val);

    switch (addr & 0xffc) {
    case 4: /* xirr */
        if (byte0) {
            icp_set_cppr(s, server, val);
        } else if (width == 4) {
            icp_eoi(s, server, val);
        } else {
            goto bad_access;
        }
        break;
    case 12:
        if (byte0) {
            icp_set_mfrr(s, server, val);
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
        val = 0xffffffff;
    }
}

static const MemoryRegionOps icp_mm_ops = {
    .read = icp_mm_read,
    .write = icp_mm_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

#define _FDT(exp) \
    do { \
        int ret = (exp);                                           \
        if (ret < 0) {                                             \
            fprintf(stderr, "qemu: error creating device tree: %s: %s\n", \
                    #exp, fdt_strerror(ret));                      \
            exit(1);                                               \
        }                                                          \
    } while (0)

void xics_create_native_icp_node(XICSState *s, void *fdt,
                                 uint32_t base, uint32_t count)
{
    uint64_t addr;
    char *name;
    const char compat[] = "IBM,power8-icp\0IBM,ppc-xicp";
    uint32_t irange[2], i, rsize;
    uint64_t *reg;

    addr = ICP_MM_BASE | (base << 12);

    irange[0] = cpu_to_be32(base);
    irange[1] = cpu_to_be32(count);

    rsize = sizeof(uint64_t) * 2 * count;
    reg = g_malloc(rsize);
    for (i = 0; i < count; i++) {
        reg[i * 2] = cpu_to_be64(addr | ((base + i) * 0x1000));
        reg[i * 2 + 1] = cpu_to_be64(0x1000);
    }

    name = g_strdup_printf("interrupt-controller@%"PRIX64, addr);

    /* interrupt controller */
    _FDT((fdt_begin_node(fdt, name)));
    g_free(name);

    _FDT((fdt_property(fdt, "compatible", compat, sizeof(compat))));
    _FDT((fdt_property(fdt, "reg", reg, rsize)));
    _FDT((fdt_property_string(fdt, "device_type",
                              "PowerPC-External-Interrupt-Presentation")));
    _FDT((fdt_property(fdt, "interrupt-controller", NULL, 0)));
    _FDT((fdt_property(fdt, "ibm,interrupt-server-ranges",
                       irange, sizeof(irange))));
    _FDT((fdt_property_cell(fdt, "#interrupt-cells", 1)));
    _FDT((fdt_property_cell(fdt, "#address-cells", 0)));
    _FDT((fdt_end_node(fdt)));
}

static void xics_native_realize(DeviceState *dev, Error **errp)
{
    XICSState *s = XICS_NATIVE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *error = NULL;
    int i;

    if (!s->nr_servers) {
        error_setg(errp, "Number of servers needs to be greater 0");
        return;
    }

    /* Register MMIO regions */
    memory_region_init_io(&s->icp_mmio, OBJECT(s), &icp_mm_ops, s, "icp",
                          ICP_MM_SIZE);
    sysbus_init_mmio(sbd, &s->icp_mmio);
    sysbus_mmio_map(sbd, 0, ICP_MM_BASE);

    for (i = 0; i < s->nr_servers; i++) {
        object_property_set_bool(OBJECT(&s->ss[i]), true, "realized", &error);
        if (error) {
            error_propagate(errp, error);
            return;
        }
    }
}

static void xics_native_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    XICSStateClass *xsc = XICS_NATIVE_CLASS(oc);

    dc->realize = xics_native_realize;
    xsc->set_nr_servers = xics_set_nr_servers;
}

static const TypeInfo xics_native_info = {
    .name          = TYPE_XICS_NATIVE,
    .parent        = TYPE_XICS_COMMON,
    .instance_size = sizeof(XICSState),
    .class_size = sizeof(XICSStateClass),
    .class_init    = xics_native_class_init,
    .instance_init = xics_native_initfn,
};

static void xics_native_register_types(void)
{
    type_register_static(&xics_native_info);
}
type_init(xics_native_register_types)
