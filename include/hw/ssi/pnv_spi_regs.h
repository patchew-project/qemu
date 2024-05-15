/*
 * QEMU PowerPC SPI Controller model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SPI_CONTROLLER_REGS_H
#define SPI_CONTROLLER_REGS_H

/* Error Register */
#define ERROR_REG                               0x00

/* counter_config_reg */
#define COUNTER_CONFIG_REG                      0x01
#define COUNTER_CONFIG_REG_SHIFT_COUNT_N1       PPC_BITMASK(0, 7)
#define COUNTER_CONFIG_REG_SHIFT_COUNT_N2       PPC_BITMASK(8, 15)
#define COUNTER_CONFIG_REG_COUNT_COMPARE1       PPC_BITMASK(24, 31)
#define COUNTER_CONFIG_REG_COUNT_COMPARE2       PPC_BITMASK(32, 39)
#define COUNTER_CONFIG_REG_N1_COUNT_CONTROL     PPC_BITMASK(48, 51)
#define COUNTER_CONFIG_REG_N2_COUNT_CONTROL     PPC_BITMASK(52, 55)

/* config_reg */
#define CONFIG_REG1                             0x02

/* clock_config_reset_control_ecc_enable_reg */
#define CLOCK_CONFIG_REG                        0x03
#define CLOCK_CONFIG_RESET_CONTROL_HARD_RESET   0x0084000000000000;
#define CLOCK_CONFIG_REG_RESET_CONTROL          PPC_BITMASK(24, 27)
#define CLOCK_CONFIG_REG_ECC_CONTROL            PPC_BITMASK(28, 30)

/* memory_mapping_reg */
#define MEMORY_MAPPING_REG                      0x04
#define MEMORY_MAPPING_REG_MMSPISM_BASE_ADDR    PPC_BITMASK(0, 15)
#define MEMORY_MAPPING_REG_MMSPISM_ADDR_MASK    PPC_BITMASK(16, 31)
#define MEMORY_MAPPING_REG_RDR_MATCH_VAL        PPC_BITMASK(32, 47)
#define MEMORY_MAPPING_REG_RDR_MATCH_MASK       PPC_BITMASK(48, 63)

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
#define STATUS_REG_SEQUENCER_FSM                PPC_BITMASK(8, 15)
#define STATUS_REG_SHIFTER_FSM                  PPC_BITMASK(16, 27)
#define STATUS_REG_SEQUENCER_INDEX              PPC_BITMASK(28, 31)
#define STATUS_REG_GENERAL_SPI_STATUS           PPC_BITMASK(32, 63)
#define STATUS_REG_RDR                          PPC_BITMASK(1, 3)
#define STATUS_REG_TDR                          PPC_BITMASK(5, 7)

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

#endif
