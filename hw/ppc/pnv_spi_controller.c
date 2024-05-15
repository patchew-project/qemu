/*
 * QEMU PowerPC SPI Controller model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ssi/pnv_spi.h"
#include "hw/ssi/pnv_spi_regs.h"
#include "hw/ssi/ssi.h"
#include "hw/ppc/fdt.h"
#include <libfdt.h>
#include <math.h>
#include "hw/irq.h"
#include "trace.h"

static uint64_t pnv_spi_controller_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    PnvSpiController *s = PNV_SPICONTROLLER(opaque);
    uint32_t reg = addr >> 3;
    uint64_t val = ~0ull;

    switch (reg) {
    case ERROR_REG:
        val = s->error_reg;
        break;
    case COUNTER_CONFIG_REG:
        val = s->counter_config_reg;
        break;
    case CONFIG_REG1:
        val = s->config_reg1;
        break;
    case CLOCK_CONFIG_REG:
        val = s->clock_config_reset_control;
        break;
    case MEMORY_MAPPING_REG:
        val = s->memory_mapping_reg;
        break;
    case TRANSMIT_DATA_REG:
        val = s->transmit_data_reg;
        break;
    case RECEIVE_DATA_REG:
        val = s->receive_data_reg;
        trace_pnv_spi_read_RDR(val);
        s->status_reg = SETFIELD(STATUS_REG_RDR_FULL, s->status_reg, 0);
        break;
    case SEQUENCER_OPERATION_REG:
        val = 0;
        for (int i = 0; i < SPI_CONTROLLER_REG_SIZE; i++) {
            val = (val << 8) | s->sequencer_operation_reg[i];
        }
        break;
    case STATUS_REG:
        val = s->status_reg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "spi_controller_regs: Invalid xscom "
                 "read at 0x%08x\n", reg);
    }

    trace_pnv_spi_read(addr, val);
    return val;
}

static void pnv_spi_controller_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PnvSpiController *s = PNV_SPICONTROLLER(opaque);
    uint32_t reg = addr >> 3;

    trace_pnv_spi_write(addr, val);

    switch (reg) {
    case ERROR_REG:
        s->error_reg = val;
        break;
    case COUNTER_CONFIG_REG:
        s->counter_config_reg = val;
        break;
    case CONFIG_REG1:
        s->config_reg1 = val;
        break;
    case CLOCK_CONFIG_REG:
        /*
         * To reset the SPI controller write the sequence 0x5 0xA to
         * reset_control field
         */
        if (GETFIELD(CLOCK_CONFIG_REG_RESET_CONTROL,
                                s->clock_config_reset_control) == 0x5) {
            if (GETFIELD(CLOCK_CONFIG_REG_RESET_CONTROL, val) == 0xA) {
                /* SPI controller reset sequence completed, resetting */
                s->clock_config_reset_control =
                                 CLOCK_CONFIG_RESET_CONTROL_HARD_RESET;
            } else {
                s->clock_config_reset_control = val;
            }
        } else {
            s->clock_config_reset_control = val;
        }
        break;
    case MEMORY_MAPPING_REG:
        s->memory_mapping_reg = val;
        break;
    case TRANSMIT_DATA_REG:
        /*
         * Writing to the transmit data register causes the transmit data
         * register full status bit in the status register to be set.  Writing
         * when the transmit data register full status bit is already set
         * causes a "Resource Not Available" condition.  This is not possible
         * in the model since writes to this register are not asynchronous to
         * the operation sequence like it would be in hardware.
         */
        s->transmit_data_reg = val;
        trace_pnv_spi_write_TDR(val);
        s->status_reg = SETFIELD(STATUS_REG_TDR_FULL, s->status_reg, 1);
        s->status_reg = SETFIELD(STATUS_REG_TDR_UNDERRUN, s->status_reg, 0);
        break;
    case RECEIVE_DATA_REG:
        s->receive_data_reg = val;
        break;
    case SEQUENCER_OPERATION_REG:
        for (int i = 0; i < SPI_CONTROLLER_REG_SIZE; i++) {
            s->sequencer_operation_reg[i] = (val >> (56 - i * 8)) & 0xFF;
        }
        break;
    case STATUS_REG:
        /* other fields are ignore_write */
        s->status_reg = SETFIELD(STATUS_REG_RDR_OVERRUN, s->status_reg,
                                  GETFIELD(STATUS_REG_RDR, val));
        s->status_reg = SETFIELD(STATUS_REG_TDR_OVERRUN, s->status_reg,
                                  GETFIELD(STATUS_REG_TDR, val));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "spi_controller_regs: Invalid xscom "
                 "write at 0x%08x\n", reg);
    }
    return;
}

static const MemoryRegionOps pnv_spi_controller_xscom_ops = {
    .read = pnv_spi_controller_read,
    .write = pnv_spi_controller_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static Property pnv_spi_controller_properties[] = {
    DEFINE_PROP_UINT32("spic_num", PnvSpiController, spic_num, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_spi_controller_realize(DeviceState *dev, Error **errp)
{
    PnvSpiController *s = PNV_SPICONTROLLER(dev);
    g_autofree char *name = g_strdup_printf(TYPE_PNV_SPI_BUS ".%d",
                    s->spic_num);
    s->ssi_bus = ssi_create_bus(dev, name);
    s->cs_line = g_new0(qemu_irq, 1);
    qdev_init_gpio_out_named(DEVICE(s), s->cs_line, "cs", 1);

    /* spi controller scoms */
    pnv_xscom_region_init(&s->xscom_spic_regs, OBJECT(s),
                          &pnv_spi_controller_xscom_ops, s,
                          "xscom-spi-controller-regs",
                          PNV10_XSCOM_PIB_SPIC_SIZE);
}

static int pnv_spi_controller_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int offset)
{
    PnvSpiController *s = PNV_SPICONTROLLER(dev);
    g_autofree char *name;
    int s_offset;
    const char compat[] = "ibm,power10-spi_controller";
    uint32_t spic_pcba = PNV10_XSCOM_PIB_SPIC_BASE +
        s->spic_num * PNV10_XSCOM_PIB_SPIC_SIZE;
    uint32_t reg[] = {
        cpu_to_be32(spic_pcba),
        cpu_to_be32(PNV10_XSCOM_PIB_SPIC_SIZE)
    };
    name = g_strdup_printf("spi_controller@%x", spic_pcba);
    s_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(s_offset);

    _FDT(fdt_setprop(fdt, s_offset, "reg", reg, sizeof(reg)));
    _FDT(fdt_setprop(fdt, s_offset, "compatible", compat, sizeof(compat)));
    _FDT((fdt_setprop_cell(fdt, s_offset, "spic_num#", s->spic_num)));
    return 0;
}

static void pnv_spi_controller_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xscomc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xscomc->dt_xscom = pnv_spi_controller_dt_xscom;

    dc->desc = "PowerNV SPI Controller";
    dc->realize = pnv_spi_controller_realize;
    device_class_set_props(dc, pnv_spi_controller_properties);
}

static const TypeInfo pnv_spi_controller_info = {
    .name          = TYPE_PNV_SPI_CONTROLLER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PnvSpiController),
    .class_init    = pnv_spi_controller_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_spi_controller_register_types(void)
{
    type_register_static(&pnv_spi_controller_info);
}

type_init(pnv_spi_controller_register_types);
