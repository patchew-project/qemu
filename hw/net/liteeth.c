/*
 * LiteX Liteeth Ethernet controller
 *
 * Copyright (c) 2021, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/net/liteeth.h"
#include "net/eth.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "sysemu/dma.h"
#include "trace.h"

#define LITEETH_WRITER_SLOT       (0x00 / 4)
#define LITEETH_WRITER_LENGTH     (0x04 / 4)
#define LITEETH_WRITER_ERRORS     (0x08 / 4) /* backend FIFO errors */
#define LITEETH_WRITER_EV_STATUS  (0x0C / 4) /* raw IRQ level bits */
#define LITEETH_WRITER_EV_PENDING (0x10 / 4) /* to read and clear level */
#define LITEETH_WRITER_EV_ENABLE  (0x14 / 4)
#define LITEETH_READER_START      (0x18 / 4)
#define LITEETH_READER_READY      (0x1C / 4)
#define LITEETH_READER_LEVEL      (0x20 / 4)
#define LITEETH_READER_SLOT       (0x24 / 4)
#define LITEETH_READER_LENGTH     (0x28 / 4)
#define LITEETH_READER_EV_STATUS  (0x2C / 4) /* raw IRQ level bits */
#define LITEETH_READER_EV_PENDING (0x30 / 4)
#define LITEETH_READER_EV_ENABLE  (0x34 / 4)
#define LITEETH_PREAMBLE_CRC      (0x38 / 4) /* ??? */
#define LITEETH_PREAMBLE_ERRORS   (0x3C / 4) /* ??? */
#define LITEETH_CRC_ERRORS        (0x40 / 4) /* ??? */

#define LITEETH_SLOT_SIZE         (2 * KiB)

static void liteeth_update_irq(LiteEthState *s)
{
    bool level = s->regs[LITEETH_READER_EV_PENDING] ||
        s->regs[LITEETH_WRITER_EV_PENDING];

    qemu_set_irq(s->irq, level);
}

static hwaddr liteeth_rx_addr(LiteEthState *s)
{
    return s->rx_current * LITEETH_SLOT_SIZE;
}

static hwaddr liteeth_tx_addr(LiteEthState *s)
{
    hwaddr tx_offset = s->rx_slots * LITEETH_SLOT_SIZE;
    uint8_t slot = s->regs[LITEETH_READER_SLOT];

    return tx_offset + slot * LITEETH_SLOT_SIZE;
}

static void liteeth_xmit(LiteEthState *s)
{
    uint8_t buf[LITEETH_SLOT_SIZE];
    uint16_t len = s->regs[LITEETH_READER_LENGTH];
    hwaddr addr = liteeth_tx_addr(s);
    MemTxResult result;

    trace_liteeth_xmit(len, s->regs[LITEETH_READER_SLOT]);

    assert(len <= LITEETH_SLOT_SIZE);

    result = address_space_read(&s->mmio_buf_as, addr, MEMTXATTRS_UNSPECIFIED,
                                buf, len);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: failed to read packet @0x%"HWADDR_PRIx "\n",
                      __func__, addr);
        /* TODO: report TX errors ? */
        return;
    }

    qemu_send_packet(qemu_get_queue(s->nic), buf, len);

    if (s->regs[LITEETH_READER_EV_ENABLE]) {
        s->regs[LITEETH_READER_EV_PENDING] = 1;
    }
}

static bool liteeth_can_receive(NetClientState *nc)
{
    LiteEthState *s = LITEETH(qemu_get_nic_opaque(nc));
    return s->regs[LITEETH_WRITER_EV_ENABLE];
}

static ssize_t liteeth_receive(NetClientState *nc, const uint8_t *buf,
                               size_t len)
{
    LiteEthState *s = LITEETH(qemu_get_nic_opaque(nc));
    hwaddr addr = liteeth_rx_addr(s);
    MemTxResult result;

    trace_liteeth_receive(len, s->rx_current);

    if (len > LITEETH_SLOT_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: frame too big : %zd bytes\n",
                      __func__, len);
        len = LITEETH_SLOT_SIZE;
    }

    /* Copy data into memory */
    result = address_space_write(&s->mmio_buf_as, addr, MEMTXATTRS_UNSPECIFIED,
                                 buf, len);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: failed to write packet @0x%"HWADDR_PRIx "\n",
                      __func__, addr);
        return -1;
    }

    /* Update registers */
    s->regs[LITEETH_WRITER_SLOT] = s->rx_current;
    s->regs[LITEETH_WRITER_LENGTH] = len;

    if (s->regs[LITEETH_WRITER_EV_ENABLE]) {
        s->regs[LITEETH_WRITER_EV_PENDING] = 1;
    }

    s->rx_current = (s->rx_current + 1) % s->rx_slots;

    liteeth_update_irq(s);
    return len;
}

static void liteeth_reset(DeviceState *dev)
{
    LiteEthState *s = LITEETH(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[LITEETH_READER_READY] = 1;

    s->rx_current = 0;
    qemu_set_irq(s->irq, 0);
}

static uint64_t liteeth_read(void *opaque, hwaddr addr, unsigned width)
{
    LiteEthState *s = LITEETH(opaque);
    uint32_t reg = addr >> 2;
    uint64_t val = s->regs[reg];

    trace_liteeth_read(addr, val);

    return val;
}

static void liteeth_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned width)
{
    LiteEthState *s = LITEETH(opaque);
    uint32_t reg = addr >> 2;

    trace_liteeth_write(addr, val);

    switch (reg) {
    case LITEETH_READER_START:
        if (s->regs[LITEETH_READER_EV_ENABLE]) {
            s->regs[LITEETH_READER_READY] = 0;
            liteeth_xmit(s);
            s->regs[LITEETH_READER_READY] = 1;
        }
        if (liteeth_can_receive(qemu_get_queue(s->nic))) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    case LITEETH_READER_EV_PENDING:
    case LITEETH_WRITER_EV_PENDING:
        s->regs[reg] = 0;
        break;

    case LITEETH_READER_LENGTH:
        if (val > LITEETH_SLOT_SIZE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: frame too big : %"PRIx64" bytes\n",
                          __func__, val);
            val = LITEETH_SLOT_SIZE;
        }
        s->regs[reg] = val;
        break;
    case LITEETH_READER_SLOT:
        s->regs[reg] = val % s->tx_slots;
        break;

    case LITEETH_READER_READY:
    case LITEETH_WRITER_LENGTH:
    case LITEETH_WRITER_SLOT:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid write @%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;

    default:
        s->regs[reg] = val;
    }

    liteeth_update_irq(s);
}

static const MemoryRegionOps liteeth_ops = {
    .read = liteeth_read,
    .write = liteeth_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void liteeth_cleanup(NetClientState *nc)
{
    LiteEthState *s = LITEETH(qemu_get_nic_opaque(nc));

    s->nic = NULL;
}

struct NetClientInfo net_liteeth_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = liteeth_can_receive,
    .receive = liteeth_receive,
    .cleanup = liteeth_cleanup,
};

static void liteeth_realize(DeviceState *dev, Error **errp)
{
    LiteEthState *s = LITEETH(dev);
    Error *err = NULL;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    uint64_t membuf_size = (s->tx_slots + s->rx_slots) * LITEETH_SLOT_SIZE;

    sysbus_init_irq(sbd, &s->irq);

    /* MAC registers */
    memory_region_init_io(&s->mmio, OBJECT(s), &liteeth_ops, s,
                          TYPE_LITEETH "-regs", 0x44);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    /* Packet buffers */
    memory_region_init(&s->mmio_buf_container, OBJECT(s),
                       TYPE_LITEETH "-buf-container", membuf_size);
    sysbus_init_mmio(sbd, &s->mmio_buf_container);

    memory_region_init_ram(&s->mmio_buf, OBJECT(s), TYPE_LITEETH "-buf",
                           membuf_size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(&s->mmio_buf_container, 0x0, &s->mmio_buf);

    address_space_init(&s->mmio_buf_as, &s->mmio_buf, TYPE_LITEETH "-buf");


    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_liteeth_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static Property liteeth_properties[] = {
    DEFINE_PROP_UINT32("tx-slots", struct LiteEthState, tx_slots, 2),
    DEFINE_PROP_UINT32("rx-slots", struct LiteEthState, rx_slots, 2),
    DEFINE_NIC_PROPERTIES(struct LiteEthState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void liteeth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc    = "LiteX Ethernet";
    dc->realize = liteeth_realize;
    dc->reset = liteeth_reset;
    device_class_set_props(dc, liteeth_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo liteeth_info = {
    .name = TYPE_LITEETH,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct LiteEthState),
    .class_init = liteeth_class_init,
};

static void liteeth_register_types(void)
{
    type_register_static(&liteeth_info);
}

type_init(liteeth_register_types);
