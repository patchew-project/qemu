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
 */

#ifndef PPC_PNV_SPI_CONTROLLER_H
#define PPC_PNV_SPI_CONTROLLER_H

#define TYPE_PNV_SPI_CONTROLLER "pnv-spi-controller"
#define PNV_SPICONTROLLER(obj) \
        OBJECT_CHECK(PnvSpiController, (obj), TYPE_PNV_SPI_CONTROLLER)

#define SPI_CONTROLLER_REG_SIZE 8

typedef struct SpiBus SpiBus;

typedef struct PnvSpiController {
    DeviceState parent;

    SpiBus            *spi_bus;
    MemoryRegion    xscom_spic_regs;
    /* SPI controller object number */
    uint32_t        spic_num;

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
