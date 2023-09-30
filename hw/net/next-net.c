/*
 * QEMU NeXT Network (MB8795) emulation
 *
 * Copyright (c) 2011 Bryce Lanham
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
#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/irq.h"
#include "hw/m68k/next-cube.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "net/net.h"

/* debug NeXT ethernet */
// #define DEBUG_NET

#ifdef DEBUG_NET
#define DPRINTF(fmt, ...) \
    do { printf("NET: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

/* names could be better */
typedef struct NextDMA {
    uint32_t csr;
    uint32_t savedbase;
    uint32_t savedlimit;

    uint32_t baser;
    uint32_t base;
    uint32_t limit;
    uint32_t chainbase;
    uint32_t chainlimit;
    uint32_t basew;
} NextDMA;

typedef struct NextNetState {
    SysBusDevice parent_obj;

    MemoryRegion mr[4];
    qemu_irq irq[NEXTNET_NUM_IRQS];
    uint8_t mac[6];

    NICState *nic;
    NICConf conf;

    NextDMA tx_dma;
    uint8_t tx_stat;
    uint8_t tx_mask;
    uint8_t tx_mode;

    NextDMA rx_dma;
    uint8_t rx_stat;
    uint8_t rx_mask;
    uint8_t rx_mode;

    uint8_t rst_mode;
} NextNetState;

#define NEXT_NET(obj) OBJECT_CHECK(NextNetState, (obj), TYPE_NEXT_NET)

static ssize_t nextnet_rx(NetClientState *nc, const uint8_t *buf, size_t size);


static uint64_t nextnet_mmio_rd_dma(void *opaque, hwaddr addr, unsigned size)
{
    NextNetState *s = (NextNetState *)opaque;

    g_assert(size == 4);

    addr += 0x110;
    switch (addr) {
    case 0x110:
        DPRINTF("TXCSR Read\n");
        return s->tx_dma.csr;
    case 0x150:
        DPRINTF("RXCSR Read %x\n", s->rx_dma.csr);
        return s->rx_dma.csr;
    default:
        g_assert_not_reached();
    }
}

static void nextnet_mmio_wr_dma(void *opaque, hwaddr addr, uint64_t value,
                                unsigned size)
{
    NextNetState *s = (NextNetState *)opaque;

    g_assert(size == 4);

    addr += 0x110;
    switch (addr) {
    case 0x110:
        if (value & DMA_SETENABLE) {
            size_t len = (0xFFFFFFF & s->tx_dma.limit) - s->tx_dma.base;
            uint8_t buf[len]; /* needs to be in dma struct? */

            DPRINTF("TXDMA ENABLE: %x len: %zu\n", s->tx_dma.base, len);
            cpu_physical_memory_read(s->tx_dma.base, buf, len);

            qemu_send_packet(qemu_get_queue(s->nic), buf, len);
            s->tx_dma.csr |= DMA_COMPLETE | DMA_SUPDATE;
            s->tx_stat = 0x80;

            qemu_set_irq(s->irq[NEXTNET_TX_I_DMA], true);
        }
        if (value & DMA_SETSUPDATE) {
            s->tx_dma.csr |= DMA_SUPDATE;
        }
        if (value & DMA_CLRCOMPLETE) {
            s->tx_dma.csr &= ~DMA_COMPLETE;
            qemu_set_irq(s->irq[NEXTNET_TX_I_DMA], false); /* TODO: OK here? */
        }
        if (value & DMA_RESET) {
            s->tx_dma.csr &= ~(DMA_COMPLETE | DMA_SUPDATE | DMA_ENABLE);
        }
        break;

    case 0x150:
        if (value & DMA_DEV2M) {
            DPRINTF("RX Dev to Memory\n");
        }

        if (value & DMA_SETENABLE) {
            s->rx_dma.csr |= DMA_ENABLE;
        }
        if (value & DMA_SETSUPDATE) {
            s->rx_dma.csr |= DMA_SUPDATE;
        }

        if (value & DMA_CLRCOMPLETE) {
            s->rx_dma.csr &= ~DMA_COMPLETE;
            qemu_set_irq(s->irq[NEXTNET_RX_I_DMA], false); /* TODO: OK here? */
        }
        if (value & DMA_RESET) {
            s->rx_dma.csr &= ~(DMA_COMPLETE | DMA_SUPDATE | DMA_ENABLE);
        }

        DPRINTF("RXCSR \tWrite: %"HWADDR_PRIx"\n", value);
        break;

    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps nextnet_mmio_ops_dma = {
    .read = nextnet_mmio_rd_dma,
    .write = nextnet_mmio_wr_dma,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t nextnet_mmio_rd_chan1(void *opaque, hwaddr addr, unsigned size)
{
    NextNetState *s = (NextNetState *)opaque;

    g_assert(size == 4);

    addr += 0x4100;
    switch (addr) {
    case 0x4100:
        DPRINTF("SAVEDBASE Read\n");
        return s->tx_dma.savedbase;
    case 0x4104:
        DPRINTF("SAVELIMIT Read\n");
        return s->tx_dma.savedlimit;
    case 0x4114:
        DPRINTF("TXLIMIT Read\n");
        return s->tx_dma.limit;
    case 0x4140:
        return s->rx_dma.savedbase;
    case 0x4144:
        // DPRINTF("SAVELIMIT %x @ %x\n",s->rx_dma.savedlimit, s->pc);
        return s->rx_dma.savedlimit;
    default:
        DPRINTF("NET Read l @ %x\n", (unsigned int)addr);
        return 0;
    }
}

static void nextnet_mmio_wr_chan1(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    NextNetState *s = (NextNetState *)opaque;
    uint32_t value = val;

    g_assert(size == 4);

    addr += 0x4100;
    switch (addr) {
    case 0x4100:
        DPRINTF("Write l @ %x with %x\n", (unsigned int)addr, value);
        s->tx_dma.savedbase = value;
        break;
    case 0x4104:
        DPRINTF("Write l @ %x with %x\n", (unsigned int)addr, value);
        s->tx_dma.savedlimit = value;
        break;
    case 0x4110:
        DPRINTF("Write l @ %x with %x\n", (unsigned int)addr, value);
        s->tx_dma.base = value;
        break;
    case 0x4114:
        DPRINTF("Write l @ %x with %x\n", (unsigned int)addr, value);
        s->tx_dma.limit = value;
        break;
    case 0x4150:
        // DPRINTF("Write l @ %x with %x\n",addr,value);
        s->rx_dma.base = value;
        // s->rx_dma.savedbase = value;
        break;
    case 0x4154:
        s->rx_dma.limit = value;
        // DPRINTF("Write l @ %x with %x\n",addr,value);
        break;
    case 0x4158:
        s->rx_dma.chainbase = value;
        // DPRINTF("Write l @ %x with %x\n",addr,value);
        break;
    case 0x415c:
        s->rx_dma.chainlimit = value;
        // DPRINTF("Write l @ %x with %x\n",addr,value);
        //DPRINTF("Pointer write %x w %x\n",addr,value);
        break;
    default:
        DPRINTF("Write l @ %x with %x\n", (unsigned int)addr, value);
    }
}

static const MemoryRegionOps nextnet_mmio_ops_chan1 = {
    .read = nextnet_mmio_rd_chan1,
    .write = nextnet_mmio_wr_chan1,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t nextnet_mmio_rd_chan2(void *opaque, hwaddr addr, unsigned size)
{
    NextNetState *s = (NextNetState *)opaque;

    g_assert(size == 4);

    addr += 0x4310;
    switch (addr) {
    case 0x4310:
        DPRINTF("TXBASE Read\n");
        /* FUTURE :return nextdma_read(device, addr); */
        return s->tx_dma.basew;
    default:
        DPRINTF("NET Read l @ %x\n", (unsigned int)addr);
        return 0;
    }
}

static void nextnet_mmio_wr_chan2(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    NextNetState *s = (NextNetState *)opaque;

    g_assert(size == 4);

    addr += 0x4310;
    switch (addr) {
    case 0x4310:
        DPRINTF("Write l @ %x with %"HWADDR_PRIx"\n", (unsigned int)addr, val);
        s->tx_dma.base = val;
        /* FUTURE :nextdma_write(device, addr, value); */
        break;
    default:
        DPRINTF("Write l @ %x with %"HWADDR_PRIx"\n", (unsigned int)addr, val);
    }
}

static const MemoryRegionOps nextnet_mmio_ops_chan2 = {
    .read = nextnet_mmio_rd_chan2,
    .write = nextnet_mmio_wr_chan2,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* It's likely that all register reads are bytes, while all CSR r/w are longs */
static uint64_t nextnet_mmio_rd_cnf(void *opaque, hwaddr addr, unsigned size)
{
    NextNetState *s = (NextNetState *)opaque;

    g_assert(size == 1);

    addr += 0x6000;

    switch (addr) {
    case 0x6000: /* TXSTAT */
        DPRINTF("TXSTAT \tRead\n");
        return s->tx_stat;
    case 0x6001:
        DPRINTF("TXMASK \tRead\n");
        return s->tx_mask;
    case 0x6002:
        DPRINTF("RXSTAT \tRead %x\n", s->rx_stat);
        return s->rx_stat;
    case 0x6003:
        // DPRINTF("RXMASK \tRead\n");
        return s->rx_mask;
    case 0x6004:
        DPRINTF("TXMODE \tRead\n");
        return s->tx_mode;
    case 0x6005:
        // DPRINTF("RXMODE \tRead\n");
        return s->rx_mode;
    case 0x6006:
        DPRINTF("RSTMODE \tRead\n");
        return s->rst_mode;
    default:
        DPRINTF("NET Read B @ %x\n", (unsigned int)addr);
        return 0;
    }
}

#define NET_TXSTAT_CLEAR 0xFF
#define NET_RXSTAT_CLEAR 0xFF
static void nextnet_mmio_wr_cnf(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    NextNetState *s = (NextNetState *)opaque;
    uint32_t value = val;

    g_assert(size == 1);

    addr += 0x6000;

    switch (addr) {
    case 0x6000:
        DPRINTF("TXSTAT \tWrite: %x\n", value);
        if (value == NET_TXSTAT_CLEAR) {
            s->tx_stat = 0x80;
        } else {
            s->tx_stat = value;
        }
        break;
    case 0x6001:
        DPRINTF("TXMASK \tWrite: %x\n", value);
        s->tx_mask = value;
        break;
    case 0x6002:
        // DPRINTF("RXSTAT \tWrite: %x\n", value);
        if (value == NET_RXSTAT_CLEAR) {
            s->rx_stat = 0x80;
        } else {
            s->rx_stat = value;
        }
        break;
    case 0x6003:
        // DPRINTF("RXMASK \tWrite: %x\n", value);
        s->rx_mask = value;
        break;
    case 0x6004:
        DPRINTF("TXMODE \tWrite: %x\n", value);
        s->tx_mode = value;
        break;
    case 0x6005:
        // DPRINTF("RXMODE \tWrite: %x\n", value);
        s->rx_mode = value;
        break;
    case 0x6006:
        DPRINTF("RSTMODE \tWrite: %x\n", value);
        s->rst_mode = value;
        break;
    case 0x600d:
        s->mac[(addr & 0xF) - 8] = value;
        DPRINTF("Set MAC ADDR %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n", s->mac[0],
                s->mac[1], s->mac[2], s->mac[3], s->mac[4], s->mac[5]);
        qemu_macaddr_default_if_unset((MACAddr *)&s->mac);
        break;
    case 0x6008:
    case 0x6009:
    case 0x600a:
    case 0x600b:
    case 0x600c:
        s->mac[(addr & 0xF) - 8] = value;
        break;
    case 0x6010:
    case 0x6011:
    case 0x6012:
    case 0x6013:
    case 0x6014:
        break;
    default:
        DPRINTF(" Write B @ %x with %x\n", (unsigned int)addr, value);
        g_assert_not_reached();
    }
}

static const MemoryRegionOps nextnet_mmio_ops_cnf = {
    .read = nextnet_mmio_rd_cnf,
    .write = nextnet_mmio_wr_cnf,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static bool nextnet_can_rx(NetClientState *nc)
{
    NextNetState *s = qemu_get_nic_opaque(nc);

    return (s->rx_mode & 0x3) != 0;
}

static ssize_t nextnet_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    NextNetState *s = qemu_get_nic_opaque(nc);

    DPRINTF("received packet %zu\n", size);

    /* Ethernet DMA is supposedly 32 byte aligned */
    if ((size % 32) != 0) {
        size -= size % 32;
        size += 32;
    }

    /* Write the packet into memory */
    cpu_physical_memory_write(s->rx_dma.base, buf, size);

    /*
     * Saved limit is checked to calculate packet size by both the rom
     * and netbsd
     */
    s->rx_dma.savedlimit = (s->rx_dma.base + size);
    s->rx_dma.savedbase = (s->rx_dma.base);

    /*
     * 32 bytes under savedbase seems to be some kind of register
     * of which the purpose is unknown as of yet
     */
    //stl_phys(s->rx_dma.base-32, 0xFFFFFFFF);

    if ((s->rx_dma.csr & DMA_SUPDATE)) {
        s->rx_dma.base = s->rx_dma.chainbase;
        s->rx_dma.limit = s->rx_dma.chainlimit;
    }
    /* we received a packet */
    s->rx_stat = 0x80;

    /* Set dma registers and raise an irq */
    s->rx_dma.csr |= DMA_COMPLETE; /* DON'T CHANGE THIS! */
    qemu_set_irq(s->irq[NEXTNET_RX_I_DMA], true);

    return size;
}

static NetClientInfo nextnet_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = nextnet_rx,
    .can_receive = nextnet_can_rx,
    .receive = nextnet_rx,
};

static void nextnet_realize(DeviceState *dev, Error **errp)
{
    NextNetState *s = NEXT_NET(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    uint8_t mac[6] = { 0x00, 0x00, 0x0f, 0x00, 0xf3, 0x02 };
    int i;

    memcpy(&s->mac, mac, 6);  
    s->nic = qemu_new_nic(&nextnet_info, &s->conf, "NeXT MB8795", dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->mac);

    /* Init device register spaces */
    memory_region_init_io(&s->mr[0], NULL, &nextnet_mmio_ops_dma, s,
                          "next.net.dma", 0x60);
    sysbus_init_mmio(sbd, &s->mr[0]);
    memory_region_init_io(&s->mr[1], NULL, &nextnet_mmio_ops_chan1, s,
                          "next.net.chan1", 0x80);
    sysbus_init_mmio(sbd, &s->mr[1]);
    memory_region_init_io(&s->mr[2], NULL, &nextnet_mmio_ops_chan2, s,
                          "next.net.chan2", 0x60);
    sysbus_init_mmio(sbd, &s->mr[2]);
    memory_region_init_io(&s->mr[3], NULL, &nextnet_mmio_ops_cnf, s,
                          "next.net.cnf", 0x20);
    sysbus_init_mmio(sbd, &s->mr[3]);

    for (i = 0; i < NEXTNET_NUM_IRQS; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
}

static Property nextnet_properties[] = {
    DEFINE_NIC_PROPERTIES(NextNetState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void nextnet_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->realize = nextnet_realize;
    dc->desc = "NeXT Ethernet Controller";
    device_class_set_props(dc, nextnet_properties);
}

static const TypeInfo nextnet_typeinfo = {
    .name          = TYPE_NEXT_NET,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NextNetState),
    .class_init    = nextnet_class_init,
};

static void nextnet_register_types(void)
{
    type_register_static(&nextnet_typeinfo);
}

type_init(nextnet_register_types)
