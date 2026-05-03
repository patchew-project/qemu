/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU AMD PC-Net II (Am79C970A) emulation
 *
 * Copyright (c) 2004 Antony T Curtis
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
 */

/*
 * This software was written to be compatible with the specification:
 * AMD Am79C970A PCnet-PCI II Ethernet Controller Data-Sheet
 * AMD Publication# 19436  Rev:E  Amendment/0  Issue Date: June 2000
 */

/*
 * On Sparc32, this is the Lance (Am7990) part of chip STP2000 (Master I/O), also
 * produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR92C990.txt
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sparc/sparc32_dma.h"
#include "migration/vmstate.h"
#include "hw/net/lance.h"
#include "hw/core/qdev-properties.h"
#include "trace.h"
#include "system/system.h"
#include "system/address-spaces.h"
#include "system/dma.h"

/*
 * LANCE Native DMA Read Hook
 *
 * Sun-3 Hardware intrinsically byte-swaps the D0-D7 and D8-D15 DMA lanes
 * between the Little-Endian LANCE controller and the Big-Endian Sun-3 memory.
 * We MUST model this manually. pcnet.c passes CSR_BSWP(s) for payloads
 * (correctly bypassing the swap if LANCE internally neutralizes it).
 * However, pcnet.c hardcodes do_bswap=1 for the `initblk` structure
 * (len 24/28).
 * We dynamically intercept the `initblk` fetch by cross-referencing CSR_IADR to
 * enforce the hardware swap reliably. Because this branch is only taken when
 * `dma_mr` is explicitly provided by the machine, this quirk is safely isolated
 * to the Sun-3 and does not impact SPARC (which uses `ledma`).
 */
static void lance_dma_read(void *dma_opaque, hwaddr addr,
                           uint8_t *buf, int len, int do_bswap)
{
    SysBusPCNetState *d = SYSBUS_PCNET(dma_opaque);
    PCNetState *s = &d->state;

    dma_memory_read(&d->dma_as, addr, buf, len, MEMTXATTRS_UNSPECIFIED);

    uint32_t bcr_ssize32 = (s->bcr[20] & 0x0100);
    hwaddr iadr = (s->csr[1] | ((uint32_t)s->csr[2] << 16));
    if (!bcr_ssize32) {
        iadr |= ((0xff00 & (uint32_t)s->csr[2]) << 16);
    }

    int internal_bswap = do_bswap;
    if (addr == iadr && (len == 24 || len == 28)) {
        internal_bswap = 0; /* Force hardware swap natively for initblk */
    }

    if (!internal_bswap) {
        for (int i = 0; i < (len & ~1); i += 2) {
            uint8_t tmp = buf[i];
            buf[i] = buf[i + 1];
            buf[i + 1] = tmp;
        }
    }
}

/* LANCE Native DMA Write Hook */
static void lance_dma_write(void *dma_opaque, hwaddr addr,
                            uint8_t *buf, int len, int do_bswap)
{
    SysBusPCNetState *s = SYSBUS_PCNET(dma_opaque);

    if (!do_bswap) {
        uint8_t *swapped_buf = g_malloc(len);
        for (int i = 0; i < (len & ~1); i += 2) {
            swapped_buf[i] = buf[i + 1];
            swapped_buf[i + 1] = buf[i];
        }
        if (len & 1) {
            swapped_buf[len - 1] = buf[len - 1];
        }
        dma_memory_write(&s->dma_as, addr, swapped_buf, len,
                         MEMTXATTRS_UNSPECIFIED);
        g_free(swapped_buf);
    } else {
        dma_memory_write(&s->dma_as, addr, buf, len, MEMTXATTRS_UNSPECIFIED);
    }
}
static void parent_lance_reset(void *opaque, int irq, int level)
{
    SysBusPCNetState *d = opaque;
    if (level)
        pcnet_h_reset(&d->state);
}

static void lance_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    SysBusPCNetState *d = opaque;

    trace_lance_mem_writew(addr, val & 0xffff);
    if (size == 1) {
        uint16_t orig = pcnet_ioport_readw(&d->state, addr & ~1);
        if (addr & 1) { /* LSB in Big Endian */
            val = (orig & 0xff00) | (val & 0xff);
        } else { /* MSB in Big Endian */
            val = (orig & 0x00ff) | ((val & 0xff) << 8);
        }
    }
    pcnet_ioport_writew(&d->state, addr & ~1, val & 0xffff);
}

static uint64_t lance_mem_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    SysBusPCNetState *d = opaque;
    uint32_t val;

    val = pcnet_ioport_readw(&d->state, addr & ~1);
    if (size == 1) {
        if (addr & 1) {
            val = val & 0xff;
        } else {
            val = (val >> 8) & 0xff;
        }
    }
    return val & 0xffff;
}

static const MemoryRegionOps lance_mem_ops = {
    .read = lance_mem_read,
    .write = lance_mem_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static NetClientInfo net_lance_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = pcnet_receive,
    .link_status_changed = pcnet_set_link_status,
};

static const VMStateDescription vmstate_lance = {
    .name = "pcnet",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(state, SysBusPCNetState, 0, vmstate_pcnet, PCNetState),
        VMSTATE_END_OF_LIST()
    }
};

static void lance_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusPCNetState *d = SYSBUS_PCNET(dev);
    PCNetState *s = &d->state;

    memory_region_init_io(&s->mmio, OBJECT(d), &lance_mem_ops, d,
                          "lance-mmio", 4);

    qdev_init_gpio_in(dev, parent_lance_reset, 1);

    sysbus_init_mmio(sbd, &s->mmio);

    sysbus_init_irq(sbd, &s->irq);

    if (d->dma_mr) {
        address_space_init(&d->dma_as, d->dma_mr, "lance-dma");
        s->phys_mem_read = lance_dma_read;
        s->phys_mem_write = lance_dma_write;
        s->dma_opaque = DEVICE(d);
    } else {
#if defined(TARGET_SPARC)
    s->phys_mem_read = ledma_memory_read;
    s->phys_mem_write = ledma_memory_write;
#endif
    }

    pcnet_common_init(dev, s, &net_lance_info);
}

static void lance_reset(DeviceState *dev)
{
    SysBusPCNetState *d = SYSBUS_PCNET(dev);

    pcnet_h_reset(&d->state);
}

static void lance_instance_init(Object *obj)
{
    SysBusPCNetState *d = SYSBUS_PCNET(obj);
    PCNetState *s = &d->state;

    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static const Property lance_properties[] = {
    DEFINE_PROP_LINK("dma", SysBusPCNetState, state.dma_opaque,
                     TYPE_DEVICE, DeviceState *),
    DEFINE_PROP_LINK("dma_mr", SysBusPCNetState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_NIC_PROPERTIES(SysBusPCNetState, state.conf),
};

static void lance_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lance_realize;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->fw_name = "ethernet";
    device_class_set_legacy_reset(dc, lance_reset);
    dc->vmsd = &vmstate_lance;
    device_class_set_props(dc, lance_properties);
}

static const TypeInfo lance_info = {
    .name          = TYPE_LANCE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusPCNetState),
    .class_init    = lance_class_init,
    .instance_init = lance_instance_init,
};

static void lance_register_types(void)
{
    type_register_static(&lance_info);
}

type_init(lance_register_types)
