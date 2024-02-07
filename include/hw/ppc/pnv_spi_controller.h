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

#ifndef PPC_PNV_SPI_CONTROLLER_H
#define PPC_PNV_SPI_CONTROLLER_H

#define TYPE_PNV_SPI_CONTROLLER "pnv-spi-controller"
#define PNV_SPICONTROLLER(obj) \
        OBJECT_CHECK(PnvSpiController, (obj), TYPE_PNV_SPI_CONTROLLER)

#define SPI_CONTROLLER_REG_SIZE 8

typedef struct SpiBus SpiBus;
typedef struct xfer_buffer xfer_buffer;

typedef struct PnvSpiController {
    DeviceState parent;

    SpiBus            *spi_bus;
    MemoryRegion    xscom_spic_regs;
    /* SPI controller object number */
    uint32_t        spic_num;
    uint8_t         responder_select;
    /* To verify if shift_n1 happens prior to shift_n2 */
    bool            shift_n1_done;
    /*
     * Internal flags for the first and last indicators for the SPI
     * interface methods
     */
    uint8_t         first;
    uint8_t         last;
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
    /*
     * Setting this attribute to true will cause the engine to reverse the
     * bit order of each byte it appends to a payload before sending the
     * payload to a device. There may be cases where an end device expects
     * a reversed order, like in the case of the Nuvoton TPM device. The
     * order of bytes in the payload is not reversed, only the order of the
     * 8 bits in each payload byte.
     */
    bool            reverse_bits;

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

void log_all_N_counts(PnvSpiController *spi_controller);
void spi_response(PnvSpiController *spi_controller, int bits,
                xfer_buffer *rsp_payload);
void operation_sequencer(PnvSpiController *spi_controller);
bool operation_shiftn1(PnvSpiController *spi_controller, uint8_t opcode,
                       xfer_buffer **payload, bool send_n1_alone);
bool operation_shiftn2(PnvSpiController *spi_controller, uint8_t opcode,
                       xfer_buffer **payload);
bool does_rdr_match(PnvSpiController *spi_controller);
uint8_t get_from_offset(PnvSpiController *spi_controller, uint8_t offset);
void shift_byte_in(PnvSpiController *spi_controller, uint8_t byte);
void calculate_N1(PnvSpiController *spi_controller, uint8_t opcode);
void calculate_N2(PnvSpiController *spi_controller, uint8_t opcode);
void do_reset(PnvSpiController *spi_controller);
uint8_t reverse_bits8(uint8_t x);
#endif /* PPC_PNV_SPI_CONTROLLER_H */
