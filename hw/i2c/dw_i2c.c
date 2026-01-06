/*
 *  DesignWare I2C Bus Serial Interface Emulation
 *
 *  Copyright (C) 2026 Alano Song <AlanoSong@163.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/i2c/dw_i2c.h"
#include "hw/core/irq.h"
#include "hw/i2c/i2c.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

static const char *dw_i2c_get_regname(uint64_t offset)
{
    switch (offset) {
    case DW_IC_CON: return "CON";
    case DW_IC_TAR: return "TAR";
    case DW_IC_SAR: return "SAR";
    case DW_IC_DATA_CMD: return "DATA_CMD";
    case DW_IC_SS_SCL_HCNT: return "SS_SCL_HCNT";
    case DW_IC_SS_SCL_LCNT: return "SS_SCL_LCNT";
    case DW_IC_FS_SCL_HCNT: return "FS_SCL_HCNT";
    case DW_IC_FS_SCL_LCNT: return "FS_SCL_LCNT";
    case DW_IC_INTR_STAT: return "INTR_STAT";
    case DW_IC_INTR_MASK: return "INTR_MASK";
    case DW_IC_RAW_INTR_STAT: return "RAW_INTR_STAT";
    case DW_IC_RX_TL: return "RX_TL";
    case DW_IC_TX_TL: return "TX_TL";
    case DW_IC_CLR_INTR: return "CLR_INTR";
    case DW_IC_CLR_RX_UNDER: return "CLR_RX_UNDER";
    case DW_IC_CLR_RX_OVER: return "CLR_RX_OVER";
    case DW_IC_CLR_TX_OVER: return "CLR_TX_OVER";
    case DW_IC_CLR_RD_REQ: return "CLR_RD_REQ";
    case DW_IC_CLR_TX_ABRT: return "CLR_TX_ABRT";
    case DW_IC_CLR_RX_DONE: return "CLR_RX_DONE";
    case DW_IC_CLR_ACTIVITY: return "CLR_ACTIVITY";
    case DW_IC_CLR_STOP_DET: return "CLR_STOP_DET";
    case DW_IC_CLR_START_DET: return "CLR_START_DET";
    case DW_IC_CLR_GEN_CALL: return "CLR_GEN_CALL";
    case DW_IC_ENABLE: return "ENABLE";
    case DW_IC_STATUS: return "STATUS";
    case DW_IC_TXFLR: return "TXFLR";
    case DW_IC_RXFLR: return "RXFLR";
    case DW_IC_SDA_HOLD: return "SDA_HOLD";
    case DW_IC_TX_ABRT_SOURCE: return "TX_ABRT_SOURCE";
    case DW_IC_ENABLE_STATUS: return "ENABLE_STATUS";
    case DW_IC_COMP_PARAM_1: return "COMP_PARAM_1";
    case DW_IC_COMP_VERSION: return "COMP_VERSION";
    case DW_IC_COMP_TYPE: return "COMP_TYPE";
    default: return "[?]";
    }
}

/*
 * If we change reg_raw_intr_stat or reg_intr_mask,
 * must call this function to update reg_intr_stat and irq line.
 */
static void dw_i2c_update_intr(DWI2CState *s)
{
    s->reg_intr_stat = s->reg_raw_intr_stat & s->reg_intr_mask;
    if (s->reg_intr_stat) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void dw_i2c_try_clear_intr(DWI2CState *s)
{
    if (!s->reg_intr_stat) {
        qemu_irq_lower(s->irq);
    }
}

static uint32_t dw_i2c_read_data_cmd(DWI2CState *s)
{
    uint32_t byte = 0;

    if (fifo8_is_empty(&s->rx_fifo)) {
        s->reg_raw_intr_stat |= DW_IC_INTR_RX_UNDER;
        dw_i2c_update_intr(s);
    } else {
        byte = fifo8_pop(&s->rx_fifo);

        /*
         * Driver may set reg_rx_tl as 0,
         * so we also need to check if rx_fifo is empty here.
         */
        if (fifo8_num_used(&s->rx_fifo) < s->reg_rx_tl ||
            fifo8_is_empty(&s->rx_fifo)) {
            s->reg_raw_intr_stat &= ~DW_IC_INTR_RX_FULL;
            dw_i2c_update_intr(s);
        }
    }

    return byte;
}

static uint64_t dw_i2c_read(void *opaque, hwaddr offset, unsigned size)
{
    DWI2CState *s = DW_I2C(opaque);
    uint32_t val = 0;

    switch (offset) {
    case DW_IC_CON:
        val = s->reg_con;
        break;
    case DW_IC_TAR:
        val = s->reg_tar;
        break;
    case DW_IC_SAR:
        qemu_log_mask(LOG_UNIMP, "[%s]%s: slave mode not implemented\n",
                      TYPE_DW_I2C, __func__);
        break;
    case DW_IC_DATA_CMD:
        val = dw_i2c_read_data_cmd(s);
        break;
    case DW_IC_INTR_STAT:
        val = s->reg_intr_stat;
        break;
    case DW_IC_INTR_MASK:
        val = s->reg_intr_mask;
        break;
    case DW_IC_RAW_INTR_STAT:
        val = s->reg_raw_intr_stat;
        break;
    case DW_IC_RX_TL:
        val = s->reg_rx_tl;
        break;
    case DW_IC_TX_TL:
        val = s->reg_tx_tl;
        break;
    case DW_IC_CLR_INTR:
        s->reg_intr_stat = 0;
        s->reg_tx_abrt_source = 0;
        dw_i2c_try_clear_intr(s);
        break;
    case DW_IC_CLR_RX_UNDER:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_RX_UNDER;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_CLR_RX_OVER:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_RX_OVER;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_CLR_TX_OVER:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_TX_OVER;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_CLR_RD_REQ:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_RD_REQ;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_CLR_TX_ABRT:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_TX_ABRT;
        s->reg_tx_abrt_source = 0;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_CLR_RX_DONE:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_RX_DONE;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_CLR_ACTIVITY:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_ACTIVITY;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_CLR_STOP_DET:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_STOP_DET;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_CLR_START_DET:
        s->reg_raw_intr_stat &= ~DW_IC_INTR_START_DET;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_ENABLE:
        val = s->reg_enable;
        break;
    case DW_IC_STATUS:
        val = s->reg_status;
        break;
    case DW_IC_TXFLR:
        val = s->reg_txflr;
        break;
    case DW_IC_RXFLR:
        s->reg_rxflr = fifo8_num_used(&s->rx_fifo);
        val = s->reg_rxflr;
        break;
    case DW_IC_SDA_HOLD:
        val = s->reg_sda_hold;
        break;
    case DW_IC_TX_ABRT_SOURCE:
        val = s->reg_tx_abrt_source;
        break;
    case DW_IC_ENABLE_STATUS:
        val = s->reg_enable_status;
        break;
    case DW_IC_COMP_PARAM_1:
        val = s->reg_comp_param_1;
        break;
    case DW_IC_COMP_VERSION:
        val = s->reg_comp_param_ver;
        break;
    case DW_IC_COMP_TYPE:
        val = s->reg_comp_type_num;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad read addr at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_DW_I2C, __func__, offset);
        break;
    }

    trace_dw_i2c_read(DEVICE(s)->canonical_path, dw_i2c_get_regname(offset),
                      offset, val);

    return (uint64_t)val;
}

static void dw_i2c_write_con(DWI2CState *s, uint32_t val)
{
    if (!(s->reg_enable & DW_IC_ENABLE_ENABLE)) {
        s->reg_con = val;
    }
}

static void dw_i2c_write_tar(DWI2CState *s, uint32_t val)
{
    /* 10 bit address mode not support in current I2C bus core */
    if (val & DW_IC_TAR_10BITADDR_MASTER) {
        qemu_log_mask(LOG_UNIMP, "[%s]%s: 10 bit addr not implemented\n",
                      TYPE_DW_I2C, __func__);
        return;
    }

    if (!(s->reg_enable & DW_IC_ENABLE_ENABLE)) {
        /*
         * DesignWare I2C controller uses r/w bit in DW_IC_DATA_CMD
         * to indicate r/w operation, so linux driver will not set
         * the r/w bit in DW_IC_TAR, this value is the final slave
         * address on the I2C bus.
         */
        s->reg_tar = val;
        s->addr_mask = 0x7f;
    }
}

static void dw_i2c_write_data_cmd(DWI2CState *s, uint32_t val)
{
    bool no_ack = false;
    uint8_t byte = val & DW_IC_DATA_CMD_DAT_MASK;

    if (!(s->reg_enable & DW_IC_ENABLE_ENABLE)) {
        return;
    }

    if (!s->bus_active) {
        if (i2c_start_transfer(s->bus, (s->reg_tar & s->addr_mask),
                               val & DW_IC_DATA_CMD_READ)) {
            no_ack = true;
        } else {
            s->bus_active = true;
        }
    }

    if (s->bus_active) {
        if (val & DW_IC_DATA_CMD_READ) {
            byte = i2c_recv(s->bus);

            if (fifo8_is_full(&s->rx_fifo)) {
                s->reg_raw_intr_stat |= DW_IC_INTR_RX_OVER;
            } else {
                fifo8_push(&s->rx_fifo, byte);

                if (fifo8_num_used(&s->rx_fifo) >= s->reg_rx_tl) {
                    s->reg_raw_intr_stat |= DW_IC_INTR_RX_FULL;
                }
            }
        } else {
            if (i2c_send(s->bus, byte)) {
                no_ack = true;
            } else {
                s->reg_raw_intr_stat |= DW_IC_INTR_TX_EMPTY;
            }
        }
    }

    if (no_ack) {
        i2c_end_transfer(s->bus);
        s->bus_active = false;
        s->reg_raw_intr_stat |= DW_IC_INTR_TX_ABRT;
        s->reg_tx_abrt_source |= DW_IC_TX_ABRT_7B_ADDR_NOACK;
    }

    if (val & DW_IC_DATA_CMD_STOP) {
        i2c_end_transfer(s->bus);
        s->bus_active = false;

        if (val & DW_IC_DATA_CMD_READ) {
            s->reg_raw_intr_stat |= DW_IC_INTR_RX_DONE;
        }

        s->reg_raw_intr_stat |= DW_IC_INTR_STOP_DET;
    }

    dw_i2c_update_intr(s);
}

static void dw_i2c_write_enable(DWI2CState *s, uint32_t val)
{
    s->reg_enable = val;

    if (s->reg_enable & DW_IC_ENABLE_ENABLE) {
        if (i2c_scan_bus(s->bus, (s->reg_tar & s->addr_mask), 0,
                         &s->bus->current_devs)) {
            s->reg_raw_intr_stat |= DW_IC_INTR_START_DET | DW_IC_INTR_TX_EMPTY |
                                    DW_IC_INTR_ACTIVITY;
            s->reg_status |= DW_IC_STATUS_ACTIVITY;
        } else {
            s->reg_raw_intr_stat |= DW_IC_INTR_TX_ABRT;
            s->reg_status &= ~DW_IC_STATUS_ACTIVITY;
            s->reg_tx_abrt_source |= DW_IC_TX_ABRT_7B_ADDR_NOACK;
        }

        s->reg_enable_status |= DW_IC_ENABLE_STATUS_EN;
    } else {
        i2c_end_transfer(s->bus);
        fifo8_reset(&s->rx_fifo);

        s->addr_mask = 0;
        s->bus_active = false;
        s->reg_status = 0;
        s->reg_enable_status = 0;
        s->reg_raw_intr_stat  = 0;
    }

    dw_i2c_update_intr(s);
}

static void dw_i2c_write(void *opaque, hwaddr offset, uint64_t value,
                         unsigned size)
{
    DWI2CState *s = DW_I2C(opaque);
    uint32_t val = value & 0xffffffff;

    trace_dw_i2c_write(DEVICE(s)->canonical_path, dw_i2c_get_regname(offset),
                       offset, val);

    switch (offset) {
    case DW_IC_CON:
        dw_i2c_write_con(s, val);
        break;
    case DW_IC_TAR:
        dw_i2c_write_tar(s, val);
        break;
    case DW_IC_SAR:
        qemu_log_mask(LOG_UNIMP, "[%s]%s: slave mode not implemented\n",
                      TYPE_DW_I2C, __func__);
        break;
    case DW_IC_DATA_CMD:
        dw_i2c_write_data_cmd(s, val);
        break;
    case DW_IC_SS_SCL_HCNT:
        s->reg_ss_scl_hcnt = val;
        break;
    case DW_IC_SS_SCL_LCNT:
        s->reg_ss_scl_lcnt = val;
        break;
    case DW_IC_FS_SCL_HCNT:
        s->reg_fs_scl_hcnt = val;
        break;
    case DW_IC_FS_SCL_LCNT:
        s->reg_fs_scl_lcnt = val;
        break;
    case DW_IC_INTR_MASK:
        s->reg_intr_mask = val;
        dw_i2c_update_intr(s);
        break;
    case DW_IC_RX_TL:
        s->reg_rx_tl = val;
        break;
    case DW_IC_TX_TL:
        s->reg_tx_tl = val;
        break;
    case DW_IC_SDA_HOLD:
        s->reg_sda_hold = val;
        break;
    case DW_IC_ENABLE:
        dw_i2c_write_enable(s, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad write addr at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_DW_I2C, __func__, offset);
        break;
    }
}

static void dw_i2c_reset(DeviceState *dev)
{
    DWI2CState *s = DW_I2C(dev);
    s->bus_active = false;
    s->addr_mask = 0;
    fifo8_reset(&s->rx_fifo);

    s->reg_con = 0;
    s->reg_tar = 0;
    s->reg_ss_scl_hcnt = 0;
    s->reg_ss_scl_lcnt = 0;
    s->reg_fs_scl_hcnt = 0;
    s->reg_fs_scl_lcnt = 0;
    s->reg_intr_stat = 0;
    s->reg_intr_mask = 0;
    s->reg_raw_intr_stat = 0;
    s->reg_rx_tl = 0;
    s->reg_tx_tl = 0;
    s->reg_enable = 0;
    s->reg_status = 0;
    s->reg_txflr = 0;
    s->reg_rxflr = 0;
    s->reg_tx_abrt_source = 0;
    s->reg_enable_status = 0;
    s->reg_comp_param_1 = DW_IC_COMP_PARAM_1_VALUE;
    s->reg_comp_param_ver = DW_IC_SDA_HOLD_MIN_VERS;
    s->reg_comp_type_num = DW_IC_COMP_TYPE_VALUE;
}

static const MemoryRegionOps dw_i2c_ops = {
    .read = dw_i2c_read,
    .write = dw_i2c_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription dw_i2c_vmstate = {
    .name = TYPE_DW_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_FIFO8(rx_fifo, DWI2CState),
        VMSTATE_BOOL(bus_active, DWI2CState),
        VMSTATE_UINT32(addr_mask, DWI2CState),
        VMSTATE_UINT32(reg_con, DWI2CState),
        VMSTATE_UINT32(reg_tar, DWI2CState),
        VMSTATE_UINT32(reg_ss_scl_hcnt, DWI2CState),
        VMSTATE_UINT32(reg_ss_scl_lcnt, DWI2CState),
        VMSTATE_UINT32(reg_fs_scl_hcnt, DWI2CState),
        VMSTATE_UINT32(reg_fs_scl_lcnt, DWI2CState),
        VMSTATE_UINT32(reg_intr_stat, DWI2CState),
        VMSTATE_UINT32(reg_intr_mask, DWI2CState),
        VMSTATE_UINT32(reg_raw_intr_stat, DWI2CState),
        VMSTATE_UINT32(reg_rx_tl, DWI2CState),
        VMSTATE_UINT32(reg_tx_tl, DWI2CState),
        VMSTATE_UINT32(reg_sda_hold, DWI2CState),
        VMSTATE_UINT32(reg_enable, DWI2CState),
        VMSTATE_UINT32(reg_status, DWI2CState),
        VMSTATE_UINT32(reg_txflr, DWI2CState),
        VMSTATE_UINT32(reg_rxflr, DWI2CState),
        VMSTATE_UINT32(reg_tx_abrt_source, DWI2CState),
        VMSTATE_UINT32(reg_enable_status, DWI2CState),
        VMSTATE_UINT32(reg_comp_param_1, DWI2CState),
        VMSTATE_UINT32(reg_comp_param_ver, DWI2CState),
        VMSTATE_UINT32(reg_comp_type_num, DWI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void dw_i2c_realize(DeviceState *dev, Error **errp)
{
    DWI2CState *s = DW_I2C(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &dw_i2c_ops, s,
                          TYPE_DW_I2C, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    s->bus = i2c_init_bus(dev, NULL);
    fifo8_create(&s->rx_fifo, DW_I2C_RX_FIFO_DEPTH);
}

static void dw_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &dw_i2c_vmstate;
    device_class_set_legacy_reset(dc, dw_i2c_reset);
    dc->realize = dw_i2c_realize;
    dc->desc = "DesignWare I2C controller";
}

static const TypeInfo dw_i2c_type_info = {
    .name = TYPE_DW_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWI2CState),
    .class_init = dw_i2c_class_init,
};

static void dw_i2c_register_types(void)
{
    type_register_static(&dw_i2c_type_info);
}

type_init(dw_i2c_register_types)
