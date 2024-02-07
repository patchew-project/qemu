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
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_spi_controller.h"
#include "hw/ppc/pnv_spi_responder.h"
#include "hw/ppc/fdt.h"
#include <libfdt.h>
#include <math.h>

#define SPI_DEBUG(x)

/* Error Register */
#define ERROR_REG                               0x00

/* counter_config_reg */
#define COUNTER_CONFIG_REG                      0x01
#define COUNTER_CONFIG_REG_SHIFT_COUNT_N1       PPC_BITMASK(0 , 7)
#define COUNTER_CONFIG_REG_SHIFT_COUNT_N2       PPC_BITMASK(8 , 15)
#define COUNTER_CONFIG_REG_COUNT_COMPARE1       PPC_BITMASK(24 , 31)
#define COUNTER_CONFIG_REG_COUNT_COMPARE2       PPC_BITMASK(32 , 39)
#define COUNTER_CONFIG_REG_N1_COUNT_CONTROL     PPC_BITMASK(48 , 51)
#define COUNTER_CONFIG_REG_N2_COUNT_CONTROL     PPC_BITMASK(52 , 55)

/* config_reg */
#define CONFIG_REG1                             0x02

/* clock_config_reset_control_ecc_enable_reg */
#define CLOCK_CONFIG_REG                        0x03
#define CLOCK_CONFIG_RESET_CONTROL_HARD_RESET   0x0084000000000000;
#define CLOCK_CONFIG_REG_RESET_CONTROL          PPC_BITMASK(24 , 27)
#define CLOCK_CONFIG_REG_ECC_CONTROL            PPC_BITMASK(28 , 30)

/* memory_mapping_reg */
#define MEMORY_MAPPING_REG                      0x04
#define MEMORY_MAPPING_REG_MMSPISM_BASE_ADDR    PPC_BITMASK(0 , 15)
#define MEMORY_MAPPING_REG_MMSPISM_ADDR_MASK    PPC_BITMASK(16 , 31)
#define MEMORY_MAPPING_REG_RDR_MATCH_VAL        PPC_BITMASK(32 , 47)
#define MEMORY_MAPPING_REG_RDR_MATCH_MASK       PPC_BITMASK(48 , 63)

/* transmit_data_reg */
#define TRANSMIT_DATA_REG                       0x05

/* receive_data_reg */
#define RECEIVE_DATA_REG                        0x06

/* sequencer_operation_reg */
#define SEQUENCER_OPERATION_REG                 0x07

/* status_reg */
#define STATUS_REG                              0x08
#define STATUS_REG_RDR_FULL                     PPC_BIT(0)
#define STATUS_REG_RDR_OVERRUN                  PPC_BIT(1)
#define STATUS_REG_RDR_UNDERRUN                 PPC_BIT(2)
#define STATUS_REG_TDR_FULL                     PPC_BIT(4)
#define STATUS_REG_TDR_OVERRUN                  PPC_BIT(5)
#define STATUS_REG_TDR_UNDERRUN                 PPC_BIT(6)
#define STATUS_REG_SEQUENCER_FSM                PPC_BITMASK(8 , 15)
#define STATUS_REG_SHIFTER_FSM                  PPC_BITMASK(16 , 27)
#define STATUS_REG_SEQUENCER_INDEX              PPC_BITMASK(28 , 31)
#define STATUS_REG_GENERAL_SPI_STATUS           PPC_BITMASK(32 , 63)
#define STATUS_REG_RDR                          PPC_BITMASK(1 , 3)
#define STATUS_REG_TDR                          PPC_BITMASK(5 , 7)

/*
 * Shifter states
 *
 * These are the same values defined for the Shifter FSM field of the
 * status register.  It's a 12 bit field so we will represent it as three
 * nibbles in the constants.
 *
 * These are shifter_fsm values
 *
 * Status reg bits 16-27 -> field bits 0-11
 * bits 0,1,2,5 unused/reserved
 * bit 4 crc shift in (unused)
 * bit 8 crc shift out (unused)
 */

#define FSM_DONE                        0x100   /* bit 3 */
#define FSM_SHIFT_N2                    0x020   /* bit 6 */
#define FSM_WAIT                        0x010   /* bit 7 */
#define FSM_SHIFT_N1                    0x004   /* bit 9 */
#define FSM_START                       0x002   /* bit 10 */
#define FSM_IDLE                        0x001   /* bit 11 */

/*
 * Sequencer states
 *
 * These are sequencer_fsm values
 *
 * Status reg bits 8-15 -> field bits 0-7
 * bits 0-3 unused/reserved
 *
 */
#define SEQ_STATE_INDEX_INCREMENT       0x08    /* bit 4 */
#define SEQ_STATE_EXECUTE               0x04    /* bit 5 */
#define SEQ_STATE_DECODE                0x02    /* bit 6 */
#define SEQ_STATE_IDLE                  0x01    /* bit 7 */

/*
 * These are the supported sequencer operations.
 * Only the upper nibble is significant because for many operations
 * the lower nibble is a variable specific to the operation.
 */
#define SEQ_OP_STOP                     0x00
#define SEQ_OP_SELECT_SLAVE             0x10
#define SEQ_OP_SHIFT_N1                 0x30
#define SEQ_OP_SHIFT_N2                 0x40
#define SEQ_OP_BRANCH_IFNEQ_RDR         0x60
#define SEQ_OP_TRANSFER_TDR             0xC0
#define SEQ_OP_BRANCH_IFNEQ_INC_1       0xE0
#define SEQ_OP_BRANCH_IFNEQ_INC_2       0xF0


static uint64_t pnv_spi_controller_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    PnvSpiController *sc = PNV_SPICONTROLLER(opaque);
    uint32_t reg = addr >> 3;

    uint64_t val = ~0ull;

    switch (reg) {
    case ERROR_REG:
        val = sc->error_reg;
        break;
    case COUNTER_CONFIG_REG:
        val = sc->counter_config_reg;
        break;
    case CONFIG_REG1:
        val = sc->config_reg1;
        break;
    case CLOCK_CONFIG_REG:
        val = sc->clock_config_reset_control;
        break;
    case MEMORY_MAPPING_REG:
        val = sc->memory_mapping_reg;
        break;
    case TRANSMIT_DATA_REG:
        val = sc->transmit_data_reg;
        break;
    case RECEIVE_DATA_REG:
        val = sc->receive_data_reg;
        SPI_DEBUG(qemu_log("RDR being read, data extracted = 0x%16.16lx\n",
                           val));
        sc->status_reg = SETFIELD(STATUS_REG_RDR_FULL, sc->status_reg, 0);
        SPI_DEBUG(qemu_log("RDR being read, RDR_full set to 0\n"));
        break;
    case SEQUENCER_OPERATION_REG:
        for (int i = 0; i < SPI_CONTROLLER_REG_SIZE; i++) {
            val |= ((uint64_t)sc->sequencer_operation_reg[i] <<
                                                    (64 - ((i + 1) * 8)));
        }
        break;
    case STATUS_REG:
        val = sc->status_reg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "spi_controller_regs: Invalid xscom "
                 "read at 0x%08x\n", reg);
    }
    return val;
}

static void pnv_spi_controller_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PnvSpiController *sc = PNV_SPICONTROLLER(opaque);
    uint32_t reg = addr >> 3;

    switch (reg) {
    case ERROR_REG:
        sc->error_reg = val;
        break;
    case COUNTER_CONFIG_REG:
        sc->counter_config_reg = val;
        break;
    case CONFIG_REG1:
        sc->config_reg1 = val;
        break;
    case CLOCK_CONFIG_REG:
        /*
         * To reset the SPI controller write the sequence 0x5 0xA to
         * reset_control field
         */
        if (GETFIELD(CLOCK_CONFIG_REG_RESET_CONTROL,
                                sc->clock_config_reset_control) == 0x5) {
            if (GETFIELD(CLOCK_CONFIG_REG_RESET_CONTROL, val) == 0xA) {
                SPI_DEBUG(qemu_log("SPI controller reset sequence completed, "
                               "resetting..."));
                sc->clock_config_reset_control =
                                 CLOCK_CONFIG_RESET_CONTROL_HARD_RESET;
            } else {
                sc->clock_config_reset_control = val;
            }
        } else {
            sc->clock_config_reset_control = val;
        }
        break;
    case MEMORY_MAPPING_REG:
        sc->memory_mapping_reg = val;
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
        sc->transmit_data_reg = val;
        SPI_DEBUG(qemu_log("TDR being written, data written = 0x%16.16lx\n",
                            val));
        sc->status_reg = SETFIELD(STATUS_REG_TDR_FULL, sc->status_reg, 1);
        SPI_DEBUG(qemu_log("TDR being written, TDR_full set to 1\n"));
        sc->status_reg = SETFIELD(STATUS_REG_TDR_UNDERRUN, sc->status_reg, 0);
        SPI_DEBUG(qemu_log("TDR being written, TDR_underrun set to 0\n"));
        SPI_DEBUG(qemu_log("TDR being written, starting sequencer\n"));
        break;
    case RECEIVE_DATA_REG:
        sc->receive_data_reg = val;
        break;
    case SEQUENCER_OPERATION_REG:
        for (int i = 0; i < SPI_CONTROLLER_REG_SIZE; i++) {
            sc->sequencer_operation_reg[i] =
                 (val & PPC_BITMASK(i * 8 , i * 8 + 7)) >> (63 - (i * 8 + 7));
        }
        break;
    case STATUS_REG:
        ;
        uint8_t rdr_val = GETFIELD(STATUS_REG_RDR, val);
        uint8_t tdr_val = GETFIELD(STATUS_REG_TDR, val);
        /* other fields are ignore_write */
        sc->status_reg = SETFIELD(STATUS_REG_RDR_OVERRUN,
                                         sc->status_reg, rdr_val);
        sc->status_reg = SETFIELD(STATUS_REG_TDR_OVERRUN,
                                         sc->status_reg, tdr_val);
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
    PnvSpiController *sc = PNV_SPICONTROLLER(dev);
    g_autofree char *bus_name;
    bus_name = g_strdup_printf("spi_bus%x", sc->spic_num);
    sc->spi_bus = spi_create_bus(dev, bus_name);

    /* spi controller scoms */
    pnv_xscom_region_init(&sc->xscom_spic_regs, OBJECT(sc),
                          &pnv_spi_controller_xscom_ops, sc,
                          "xscom-spi-controller-regs",
                          PNV10_XSCOM_PIB_SPIC_SIZE);
}

static int pnv_spi_controller_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int offset)
{
    PnvSpiController *sc = PNV_SPICONTROLLER(dev);
    g_autofree char *name;
    int sc_offset;
    const char compat[] = "ibm,power10-spi_controller";
    uint32_t spic_pcba = PNV10_XSCOM_PIB_SPIC_BASE +
        sc->spic_num * PNV10_XSCOM_PIB_SPIC_SIZE;
    uint32_t reg[] = {
        cpu_to_be32(spic_pcba),
        cpu_to_be32(PNV10_XSCOM_PIB_SPIC_SIZE)
    };
    name = g_strdup_printf("spi_controller@%x", spic_pcba);
    sc_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(sc_offset);

    _FDT(fdt_setprop(fdt, sc_offset, "reg", reg, sizeof(reg)));
    _FDT(fdt_setprop(fdt, sc_offset, "compatible", compat, sizeof(compat)));
    _FDT((fdt_setprop_cell(fdt, sc_offset, "spic_num#", sc->spic_num)));
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
    .parent        = TYPE_DEVICE,
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
