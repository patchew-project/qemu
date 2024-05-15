/*
 * QEMU PowerPC SPI Controller model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model Supports a connection to a single SPI responder.
 * Introduced for P10 to provide access to SPI seeproms, TPM, flash device
 * and an ADC controller.
 *
 * All SPI function control is mapped into the SPI register space to enable
 * full control by firmware.
 *
 * SPI Controller has sequencer and shift engine. The SPI shift engine
 * performs serialization and de-serialization according to the control by
 * the sequencer and according to the setup defined in the configuration
 * registers and the SPI sequencer implements the main control logic.
 */
#include "hw/ssi/ssi.h"

#ifndef PPC_PNV_SPI_CONTROLLER_H
#define PPC_PNV_SPI_CONTROLLER_H

#define TYPE_PNV_SPI_CONTROLLER "pnv-spi-controller"
#define PNV_SPICONTROLLER(obj) \
        OBJECT_CHECK(PnvSpiController, (obj), TYPE_PNV_SPI_CONTROLLER)

#define SPI_CONTROLLER_REG_SIZE 8

#define TYPE_PNV_SPI_BUS "pnv-spi-bus"
typedef struct PnvSpiController {
    SysBusDevice parent_obj;

    SSIBus *ssi_bus;
    qemu_irq *cs_line;
    MemoryRegion    xscom_spic_regs;
    /* SPI controller object number */
    uint32_t        spic_num;
    uint8_t         transfer_len;
    uint8_t         responder_select;
    /* To verify if shift_n1 happens prior to shift_n2 */
    bool            shift_n1_done;
    /* Loop counter for branch operation opcode Ex/Fx */
    uint8_t         loop_counter_1;
    uint8_t         loop_counter_2;
    /* N1/N2_bits specifies the size of the N1/N2 segment of a frame in bits.*/
    uint8_t         N1_bits;
    uint8_t         N2_bits;
    /* Number of bytes in a payload for the N1/N2 frame segment.*/
    uint8_t         N1_bytes;
    uint8_t         N2_bytes;
    /* Number of N1/N2 bytes marked for transmit */
    uint8_t         N1_tx;
    uint8_t         N2_tx;
    /* Number of N1/N2 bytes marked for receive */
    uint8_t         N1_rx;
    uint8_t         N2_rx;

    /* SPI Controller registers */
    uint64_t        error_reg;
    uint64_t        counter_config_reg;
    uint64_t        config_reg1;
    uint64_t        clock_config_reset_control;
    uint64_t        memory_mapping_reg;
    uint64_t        transmit_data_reg;
    uint64_t        receive_data_reg;
    uint8_t         sequencer_operation_reg[SPI_CONTROLLER_REG_SIZE];
    uint64_t        status_reg;

} PnvSpiController;
#endif /* PPC_PNV_SPI_CONTROLLER_H */
