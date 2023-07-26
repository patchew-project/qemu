/*
 * BCM2838 Gigabit Ethernet emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "net/eth.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "net/checksum.h"
#include "sysemu/dma.h"
#include "hw/net/bcm2838_genet.h"
#include "trace.h"


static void bcm2838_genet_set_qemu_mac(BCM2838GenetState *s)
{
    s->regs.umac.mac0.fields.addr_0 = s->nic_conf.macaddr.a[0];
    s->regs.umac.mac0.fields.addr_1 = s->nic_conf.macaddr.a[1];
    s->regs.umac.mac0.fields.addr_2 = s->nic_conf.macaddr.a[2];
    s->regs.umac.mac0.fields.addr_3 = s->nic_conf.macaddr.a[3];
    s->regs.umac.mac1.fields.addr_4 = s->nic_conf.macaddr.a[4];
    s->regs.umac.mac1.fields.addr_5 = s->nic_conf.macaddr.a[5];
}

static void bcm2838_genet_set_irq_default(BCM2838GenetState *s)
{
    uint32_t intrl_0_status = s->regs.intrl0.stat.value;
    uint32_t intrl_0_mask = s->regs.intrl0.mask_status.value;
    int level = (intrl_0_status & ~intrl_0_mask) == 0 ? 0 : 1;

    qemu_set_irq(s->irq_default, level);
}

static void bcm2838_genet_set_irq_prio(BCM2838GenetState *s)
{
    uint32_t intrl_1_status = s->regs.intrl1.stat.value;
    uint32_t intrl_1_mask = s->regs.intrl1.mask_status.value;
    int level = (intrl_1_status & ~intrl_1_mask) == 0 ? 0 : 1;

    qemu_set_irq(s->irq_prio, level);
}

static uint64_t bcm2838_genet_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t value = ~0;
    BCM2838GenetState *s = opaque;

    if (offset + size < sizeof(s->regs)) {
        memcpy(&value, (uint8_t *)&s->regs + offset, size);
    } else {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
            __func__, size, offset);
    }

    trace_bcm2838_genet_read(size, offset, value);
    return value;
}

static void bcm2838_genet_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    BCM2838GenetState *s = opaque;
    NetClientState *ncs = qemu_get_queue(s->nic);
    BCM2838GenetUmacCmd umac_cmd = {.value = value};
    BCM2838GenetUmacMac0 umac_mac0 = {.value = value};
    BCM2838GenetUmacMac1 umac_mac1 = {.value = value};

    trace_bcm2838_genet_write(size, offset, value);

    if (offset + size < sizeof(s->regs)) {
        switch (offset) {
        case BCM2838_GENET_INTRL0_SET:
            s->regs.intrl0.stat.value |= value;
            break;
        case BCM2838_GENET_INTRL0_CLEAR:
            s->regs.intrl0.stat.value &= ~value;
            break;
        case BCM2838_GENET_INTRL0_MASK_SET:
            s->regs.intrl0.mask_status.value |= value;
            break;
        case BCM2838_GENET_INTRL0_MASK_CLEAR:
            s->regs.intrl0.mask_status.value &= ~value;
            break;
        case BCM2838_GENET_INTRL1_SET:
            s->regs.intrl1.stat.value |= value;
            break;
        case BCM2838_GENET_INTRL1_CLEAR:
            s->regs.intrl1.stat.value &= ~value;
            break;
        case BCM2838_GENET_INTRL1_MASK_SET:
            s->regs.intrl1.mask_status.value |= value;
            break;
        case BCM2838_GENET_INTRL1_MASK_CLEAR:
            s->regs.intrl1.mask_status.value &= ~value;
            break;
        case BCM2838_GENET_UMAC_CMD:
            /* Complete SW reset as soon as it has been requested */
            if (umac_cmd.fields.sw_reset == 1) {
                device_cold_reset(DEVICE(s));
                umac_cmd.fields.sw_reset = 0;
                value = umac_cmd.value;
            }
            break;
        /*
         * TODO: before changing MAC address we'd better inform QEMU
         * network subsystem about freeing previously used one, but
         * qemu_macaddr_set_free function isn't accessible for us (marked
         * as static in net/net.c), see also https://lists.nongnu.org/
         * archive/html/qemu-devel/2022-07/msg02123.html
         */
        case BCM2838_GENET_UMAC_MAC0:
            s->nic_conf.macaddr.a[0] = umac_mac0.fields.addr_0;
            s->nic_conf.macaddr.a[1] = umac_mac0.fields.addr_1;
            s->nic_conf.macaddr.a[2] = umac_mac0.fields.addr_2;
            s->nic_conf.macaddr.a[3] = umac_mac0.fields.addr_3;
            qemu_macaddr_default_if_unset(&s->nic_conf.macaddr);
            qemu_format_nic_info_str(ncs, s->nic_conf.macaddr.a);
            trace_bcm2838_genet_mac_address(ncs->info_str);
            break;
        case BCM2838_GENET_UMAC_MAC1:
            s->nic_conf.macaddr.a[4] = umac_mac1.fields.addr_4;
            s->nic_conf.macaddr.a[5] = umac_mac1.fields.addr_5;
            qemu_macaddr_default_if_unset(&s->nic_conf.macaddr);
            qemu_format_nic_info_str(ncs, s->nic_conf.macaddr.a);
            trace_bcm2838_genet_mac_address(ncs->info_str);
            break;
        case BCM2838_GENET_UMAC_MDIO_CMD:
        case BCM2838_GENET_TDMA_REGS
            ... BCM2838_GENET_TDMA_REGS + sizeof(BCM2838GenetRegsTdma) - 1:
            qemu_log_mask(LOG_UNIMP,
                "UMAC MDIO and TDMA aren't implemented yet");
            break;
        default:
            break;
        }

        memcpy((uint8_t *)&s->regs + offset, &value, size);
        bcm2838_genet_set_irq_default(s);
        bcm2838_genet_set_irq_prio(s);
    } else {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "%s: out-of-range access, %u bytes @ offset 0x%04" PRIx64 "\n",
            __func__, size, offset);
    }
}

static const MemoryRegionOps bcm2838_genet_ops = {
    .read = bcm2838_genet_read,
    .write = bcm2838_genet_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = sizeof(uint32_t)},
    .valid = {.min_access_size = sizeof(uint32_t)},
};

static NetClientInfo bcm2838_genet_client_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState)
};

static void bcm2838_genet_realize(DeviceState *dev, Error **errp)
{
    NetClientState *ncs;
    BCM2838GenetState *s = BCM2838_GENET(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Controller registers */
    memory_region_init_io(&s->regs_mr, OBJECT(s), &bcm2838_genet_ops, s,
                          "bcm2838_genet_regs", sizeof(s->regs));
    sysbus_init_mmio(sbd, &s->regs_mr);

    /* QEMU-managed NIC (host network back-end connection) */
    qemu_macaddr_default_if_unset(&s->nic_conf.macaddr);
    s->nic = qemu_new_nic(&bcm2838_genet_client_info, &s->nic_conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    bcm2838_genet_set_qemu_mac(s);
    ncs = qemu_get_queue(s->nic);
    qemu_format_nic_info_str(ncs, s->nic_conf.macaddr.a);
    trace_bcm2838_genet_mac_address(ncs->info_str);

    /* Interrupts */
    sysbus_init_irq(sbd, &s->irq_default);
    sysbus_init_irq(sbd, &s->irq_prio);

    /* DMA space */
    address_space_init(&s->dma_as, get_system_memory(), "bcm2838_genet_dma");
}

static void bcm2838_genet_phy_reset(BCM2838GenetState *s)
{
    memset(&s->phy_regs, 0x00, sizeof(s->phy_regs));
    memset(&s->phy_shd_regs, 0x00, sizeof(s->phy_shd_regs));
    memset(&s->phy_aux_ctl_shd_regs, 0x00, sizeof(s->phy_aux_ctl_shd_regs));

    /* All the below values were taken from the real HW trace and logs */
    s->phy_regs.bmcr.value = 0x1140;
    s->phy_regs.bmsr.value = 0x7949;
    s->phy_regs.sid1 = 0x600D;
    s->phy_regs.sid2 = 0x84A2;
    s->phy_regs.advertise = 0x01E1;
    s->phy_regs.ctrl1000 = 0x0200;
    s->phy_regs.estatus = 0x3000;

    s->phy_shd_regs.clk_ctl = 0x0200;
    s->phy_shd_regs.scr3 = 0x001F;
    s->phy_shd_regs.apd = 0x0001;

    s->phy_aux_ctl_shd_regs.misc = 0x1E;

    trace_bcm2838_genet_phy_reset("done");
}

static void bcm2838_genet_reset(DeviceState *d)
{
    BCM2838GenetState *s = BCM2838_GENET(d);

    memset(&s->regs, 0x00, sizeof(s->regs));

    /* All the below values were taken from the real HW trace and logs */
    s->regs.sys.rev_ctrl.fields.major_rev = BCM2838_GENET_REV_MAJOR;
    s->regs.sys.rev_ctrl.fields.minor_rev = BCM2838_GENET_REV_MINOR;

    trace_bcm2838_genet_reset("done");

    bcm2838_genet_phy_reset(s);
}

static Property genet_properties[] = {
    DEFINE_NIC_PROPERTIES(BCM2838GenetState, nic_conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void bcm2838_genet_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);

    dc->realize = bcm2838_genet_realize;
    dc->reset = bcm2838_genet_reset;
    device_class_set_props(dc, genet_properties);
}

static const TypeInfo bcm2838_genet_info = {
    .name       = TYPE_BCM2838_GENET,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2838GenetState),
    .class_init = bcm2838_genet_class_init,
};

static void bcm2838_genet_register(void)
{
    type_register_static(&bcm2838_genet_info);
}

type_init(bcm2838_genet_register)
