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

static void bcm2838_genet_phy_aux_ctl_write(BCM2838GenetState *s,
                                            uint16_t value)
{
    BCM2838GenetPhyAuxCtl phy_aux_ctl = {.value = value};
    uint16_t *phy_aux_ctl_shd_reg_id
        = (uint16_t *)&s->phy_aux_ctl_shd_regs + phy_aux_ctl.fields_1.reg_id;
    uint16_t *phy_aux_ctl_shd_reg_id_mask
        = (uint16_t *)&s->phy_aux_ctl_shd_regs + phy_aux_ctl.fields_1.reg_id_mask;

    if (phy_aux_ctl.fields_1.reg_id_mask == BCM2838_GENET_PHY_AUX_CTL_MISC) {
        if (phy_aux_ctl.fields_1.reg_id == BCM2838_GENET_PHY_AUX_CTL_MISC) {
            if (phy_aux_ctl.fields_1.misc_wren == 0) {
                /* write for subsequent read (8-bit from AUX_CTL_MISC) */
                phy_aux_ctl.fields_1.reg_data = *phy_aux_ctl_shd_reg_id;
            } else {
                /* write 8 bits to AUX_CTL_MISC */
                *phy_aux_ctl_shd_reg_id_mask = phy_aux_ctl.fields_1.reg_data;
            }
        } else {
            /* write for subsequent read (12-bit) */
            phy_aux_ctl.fields_2.reg_data = *phy_aux_ctl_shd_reg_id;
        }
    } else {
        /* write 12 bits */
        *phy_aux_ctl_shd_reg_id_mask = phy_aux_ctl.fields_2.reg_data;
    }

    s->phy_regs.aux_ctl.value = phy_aux_ctl.value;
}

static void bcm2838_genet_phy_shadow_write(BCM2838GenetState *s,
                                           uint16_t value)
{
    BCM2838GenetPhyShadow phy_shadow = {.value = value};
    uint16_t *phy_shd_reg
        = (uint16_t *)&s->phy_shd_regs + phy_shadow.fields.reg_id;

    if (phy_shadow.fields.wr == 0) {
        phy_shadow.fields.reg_data = *phy_shd_reg;
    } else {
        *phy_shd_reg = phy_shadow.fields.reg_data;
    }

    s->phy_regs.shd.value = phy_shadow.value;
}

static void bcm2838_genet_phy_exp_shadow_write(BCM2838GenetState *s,
                                               uint16_t value)
{
    /* TODO Stub implementation without side effect,
            just storing registers values */
    BCM2838GenetPhyExpSel* exp_ctrl = &s->phy_regs.exp_ctrl;
    s->phy_exp_shd_regs.regs[exp_ctrl->block_id][exp_ctrl->reg_id] = value;
}

static uint16_t bcm2838_genet_phy_exp_shadow_read(BCM2838GenetState *s)
{
    BCM2838GenetPhyExpSel* exp_ctrl = &s->phy_regs.exp_ctrl;
    return s->phy_exp_shd_regs.regs[exp_ctrl->block_id][exp_ctrl->reg_id];
}

static uint64_t bcm2838_genet_mdio_cmd(BCM2838GenetState *s, uint64_t cmd)
{
    BCM2838GenetUmacMdioCmd umac_mdio_cmd = {.value = cmd};
    uint8_t phy_reg_id = umac_mdio_cmd.fields.reg_id;
    uint16_t phy_reg_data = umac_mdio_cmd.fields.reg_data;
    uint16_t *phy_reg = (uint16_t *)&s->phy_regs + phy_reg_id;
    BCM2838GenetPhyBmcr phy_bmcr = {.value = phy_reg_data};

    if (umac_mdio_cmd.fields.start_busy != 0) {
        umac_mdio_cmd.fields.start_busy = 0;

        if (umac_mdio_cmd.fields.rd != 0) {
            if (phy_reg_id == BCM2838_GENET_EXP_DATA) {
                umac_mdio_cmd.fields.reg_data
                    = bcm2838_genet_phy_exp_shadow_read(s);
            } else {
                umac_mdio_cmd.fields.reg_data = *phy_reg;
            }
        } else if (umac_mdio_cmd.fields.wr != 0) {
            if (phy_reg_id == BCM2838_GENET_PHY_AUX_CTL) {
                bcm2838_genet_phy_aux_ctl_write(s, phy_reg_data);
            } else if (phy_reg_id == BCM2838_GENET_PHY_SHD) {
                bcm2838_genet_phy_shadow_write(s, phy_reg_data);
            } else if (phy_reg_id == BCM2838_GENET_EXP_DATA) {
                bcm2838_genet_phy_exp_shadow_write(s, phy_reg_data);
            } else {
                if (phy_reg_id == BCM2838_GENET_PHY_BMCR) {
                    /* Initiate auto-negotiation once it has been restarted */
                    if (phy_bmcr.fields.anrestart == 1) {
                        phy_bmcr.fields.anrestart = 0;
                        phy_reg_data = phy_bmcr.value;
                    }
                }
                *phy_reg = phy_reg_data;
            }
        }
    }

    return umac_mdio_cmd.value;
}

static void bcm2838_genet_xmit_packet(NetClientState *s, void *packet,
                                      size_t size)
{
    uint8_t *buf = packet + sizeof(BCM2838GenetXmitStatus);
    size_t len = size;
    uint16_t len_type = 0;

    len -= sizeof(BCM2838GenetXmitStatus);
    net_checksum_calculate(buf, len, CSUM_ALL);

    memcpy(&len_type, &buf[12], sizeof(len_type));
    len_type = ntohs(len_type);
    if (len_type < MAX_PAYLOAD_SIZE) {
        len_type = len;
        len_type = htons(len_type);
        memcpy(&buf[12], &len_type, sizeof(len_type));
    }

    qemu_send_packet(s, buf, len);
}

static uint64_t bcm2838_genet_tx(BCM2838GenetState *s, unsigned int ring_index,
                                 BCM2838GenetDmaProdIndex prod_index,
                                 BCM2838GenetDmaConsIndex cons_index)
{
    const unsigned int DESC_SIZE_WORDS
        = sizeof(BCM2838GenetTdmaDesc) / sizeof(uint32_t);
    const uint64_t RING_START_ADDR
        = ((uint64_t)s->regs.tdma.rings[ring_index].start_addr_hi << 32)
            + s->regs.tdma.rings[ring_index].start_addr;
    const uint64_t RING_END_ADDR
        = ((uint64_t)s->regs.tdma.rings[ring_index].end_addr_hi << 32)
            + s->regs.tdma.rings[ring_index].end_addr;

    hwaddr data_addr;
    uint64_t desc_index;
    BCM2838GenetTdmaLengthStatus desc_status;
    uint64_t num_descs = 0;
    uint64_t read_ptr
        = ((uint64_t)s->regs.tdma.rings[ring_index].read_ptr_hi << 32)
            + s->regs.tdma.rings[ring_index].read_ptr;
    off_t packet_off = 0;

    while (cons_index.fields.index != prod_index.fields.index) {
        desc_index = read_ptr / DESC_SIZE_WORDS;
        if (desc_index >= BCM2838_GENET_DMA_DESC_CNT) {
            qemu_log_mask(
                LOG_GUEST_ERROR,
                "%s: invalid TX descriptor index %" PRIu64 " (exceeds %u)\n",
                __func__, desc_index, BCM2838_GENET_DMA_DESC_CNT - 1);
            break;
        }
        desc_status.value = s->regs.tdma.descs[desc_index].length_status.value;
        data_addr = ((uint64_t)s->regs.tdma.descs[desc_index].address_hi << 32)
            + s->regs.tdma.descs[desc_index].address_lo;
        trace_bcm2838_genet_tx(ring_index, desc_index, desc_status.value,
                               data_addr);

        if (desc_status.fields.sop) {
            packet_off = 0;
        }

        /* TODO: Add address_space_read() return value check */
        address_space_read(&s->dma_as, data_addr,
                                        MEMTXATTRS_UNSPECIFIED,
                                        s->tx_packet + packet_off,
                                        desc_status.fields.buflength);
        packet_off += desc_status.fields.buflength;

        if (desc_status.fields.eop) {
            bcm2838_genet_xmit_packet(qemu_get_queue(s->nic), s->tx_packet,
                                                     packet_off);
            packet_off = 0;
        }

        num_descs++;
        cons_index.fields.index++;
        s->regs.tdma.descs[desc_index].length_status.fields.own = 1;
        read_ptr = read_ptr == RING_END_ADDR + 1 - DESC_SIZE_WORDS
            ? RING_START_ADDR : read_ptr + DESC_SIZE_WORDS;
    }

    s->regs.tdma.rings[ring_index].read_ptr = read_ptr;
    s->regs.tdma.rings[ring_index].read_ptr_hi = read_ptr >> 32;

    return num_descs;
}

static bool bcm2838_genet_tdma_ring_active(BCM2838GenetState *s,
                                           unsigned int ring_index)
{
    uint32_t ring_mask = 1 << ring_index;
    bool dma_en = s->regs.tdma.ctrl.fields.en == 1;
    bool ring_en = (s->regs.tdma.ring_cfg.fields.en & ring_mask) != 0;
    bool ring_buf_en = (s->regs.tdma.ctrl.fields.ring_buf_en & ring_mask) != 0;
    bool active = dma_en && ring_en && ring_buf_en;

    trace_bcm2838_genet_tx_dma_ring_active(ring_index,
                                           active ? "active" : "halted");
    return active;
}

static void bcm2838_genet_tdma(BCM2838GenetState *s, hwaddr offset,
                               uint64_t value)
{
    hwaddr ring_offset;
    uint64_t num_descs_tx;
    unsigned int ring_index;
    BCM2838GenetDmaConsIndex cons_index;
    BCM2838GenetDmaRingCfg ring_cfg = {.value = value};
    BCM2838GenetDmaCtrl ctrl = {.value = value};
    BCM2838GenetDmaProdIndex prod_index = {.value = value};

    switch (offset) {
    case BCM2838_GENET_TDMA_RINGS
        ... BCM2838_GENET_TDMA_RINGS + sizeof(s->regs.tdma.rings) - 1:
        ring_index = (offset - BCM2838_GENET_TDMA_RINGS)
            / sizeof(BCM2838GenetTdmaRing);
        if (bcm2838_genet_tdma_ring_active(s, ring_index)) {
            ring_offset = offset - BCM2838_GENET_TDMA_RINGS
                - ring_index * sizeof(BCM2838GenetTdmaRing);
            switch (ring_offset) {
            case BCM2838_GENET_TRING_PROD_INDEX:
                cons_index.value
                    = s->regs.tdma.rings[ring_index].cons_index.value;
                if (cons_index.fields.index != prod_index.fields.index) {
                    trace_bcm2838_genet_tx_request(ring_index,
                                                   prod_index.fields.index,
                                                   cons_index.fields.index);
                    num_descs_tx = bcm2838_genet_tx(s, ring_index, prod_index,
                                                    cons_index);
                    if (num_descs_tx > 0) {
                        s->regs.tdma.rings[ring_index].cons_index.fields.index
                            += num_descs_tx;
                        if (ring_index == BCM2838_GENET_DMA_RING_DEFAULT) {
                            s->regs.intrl0.stat.fields.txdma_mbdone = 1;
                        } else {
                            s->regs.intrl1.stat.fields.tx_intrs
                                |= 1 << ring_index;
                        }
                    }
                }
                break;
            default:
                break;
            }
        }
        break;
    case BCM2838_GENET_TDMA_RING_CFG:
        if (s->regs.tdma.ring_cfg.fields.en != ring_cfg.fields.en) {
            trace_bcm2838_genet_tx_dma_ring(ring_cfg.fields.en);
        }
        break;
    case BCM2838_GENET_TDMA_CTRL:
        if (s->regs.tdma.ctrl.fields.en != ctrl.fields.en) {
            s->regs.tdma.status.fields.disabled = s->regs.tdma.ctrl.fields.en;
            trace_bcm2838_genet_tx_dma(
                ctrl.fields.en == 1 ? "enabled" : "disabled");
        }
        if (s->regs.tdma.ctrl.fields.ring_buf_en != ctrl.fields.ring_buf_en) {
            trace_bcm2838_genet_tx_dma_ring_buf(ctrl.fields.ring_buf_en);
        }
        break;
    default:
        break;
    }
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
            value = bcm2838_genet_mdio_cmd(s, value);
            s->regs.intrl0.stat.fields.mdio_done = 1;
            break;
        case BCM2838_GENET_TDMA_REGS
            ... BCM2838_GENET_TDMA_REGS + sizeof(BCM2838GenetRegsTdma) - 1:
            bcm2838_genet_tdma(s, offset, value);
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

    bcm2838_genet_set_qemu_mac(s);
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
