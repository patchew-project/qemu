/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
/*
 * Definitions for virtio SPI Controller
 *
 * Copyright (c) 2021 Intel Corporation. All rights reserved.
 */

#ifndef _LINUX_VIRTIO_SPI_H
#define _LINUX_VIRTIO_SPI_H

#include "standard-headers/linux/const.h"
#include "standard-headers/linux/types.h"

/* Sample data on trailing clock edge */
#define VIRTIO_SPI_CPHA (1 << 0)
/* Clock is high when IDLE */
#define VIRTIO_SPI_CPOL (1 << 1)
/* Chip Select is active high */
#define VIRTIO_SPI_CS_HIGH (1 << 2)
/* Transmit LSB first */
#define VIRTIO_SPI_MODE_LSB_FIRST (1 << 3)
/* Loopback mode */
#define VIRTIO_SPI_MODE_LOOP (1 << 4)

/*
 * All config fields are read-only for the Virtio SPI driver
 *
 * @cs_max_number: maximum number of chipselect the host SPI controller
 *   supports.
 * @cs_change_supported: indicates if the host SPI controller supports to toggle
 * chipselect after each transfer in one message:
 *   0: unsupported, chipselect will be kept in active state throughout the
 *      message transaction;
 *   1: supported.
 *   Note: Message here contains a sequence of SPI transfers.
 * @tx_nbits_supported: indicates the supported number of bit for writing:
 *   bit 0: DUAL (2-bit transfer), 1 for supported
 *   bit 1: QUAD (4-bit transfer), 1 for supported
 *   bit 2: OCTAL (8-bit transfer), 1 for supported
 *   other bits are reserved as 0, 1-bit transfer is always supported.
 * @rx_nbits_supported: indicates the supported number of bit for reading:
 *   bit 0: DUAL (2-bit transfer), 1 for supported
 *   bit 1: QUAD (4-bit transfer), 1 for supported
 *   bit 2: OCTAL (8-bit transfer), 1 for supported
 *   other bits are reserved as 0, 1-bit transfer is always supported.
 * @bits_per_word_mask: mask indicating which values of bits_per_word are
 *   supported. If not set, no limitation for bits_per_word.
 * @mode_func_supported: indicates the following features are supported or not:
 *   bit 0-1: CPHA feature
 *     0b00: invalid, should support as least one CPHA setting
 *     0b01: supports CPHA=0 only
 *     0b10: supports CPHA=1 only
 *     0b11: supports CPHA=0 and CPHA=1.
 *   bit 2-3: CPOL feature
 *     0b00: invalid, should support as least one CPOL setting
 *     0b01: supports CPOL=0 only
 *     0b10: supports CPOL=1 only
 *     0b11: supports CPOL=0 and CPOL=1.
 *   bit 4: chipselect active high feature, 0 for unsupported and 1 for
 *     supported, chipselect active low should always be supported.
 *   bit 5: LSB first feature, 0 for unsupported and 1 for supported,
 *     MSB first should always be supported.
 *   bit 6: loopback mode feature, 0 for unsupported and 1 for supported,
 *     normal mode should always be supported.
 * @max_freq_hz: the maximum clock rate supported in Hz unit, 0 means no
 *   limitation for transfer speed.
 * @max_word_delay_ns: the maximum word delay supported in ns unit,
 *   0 means word delay feature is unsupported.
 *   Note: Just as one message contains a sequence of transfers,
 *         one transfer may contain a sequence of words.
 * @max_cs_setup_ns: the maximum delay supported after chipselect is asserted,
 *   in ns unit, 0 means delay is not supported to introduce after chipselect is
 *   asserted.
 * @max_cs_hold_ns: the maximum delay supported before chipselect is deasserted,
 *   in ns unit, 0 means delay is not supported to introduce before chipselect
 *   is deasserted.
 * @max_cs_incative_ns: maximum delay supported after chipselect is deasserted,
 *   in ns unit, 0 means delay is not supported to introduce after chipselect is
 *   deasserted.
 */
struct virtio_spi_config {
	/* # of /dev/spidev<bus_num>.CS with CS=0..chip_select_max_number -1 */
        uint8_t cs_max_number;
        uint8_t cs_change_supported;
#define VIRTIO_SPI_RX_TX_SUPPORT_DUAL (1 << 0)
#define VIRTIO_SPI_RX_TX_SUPPORT_QUAD (1 << 1)
#define VIRTIO_SPI_RX_TX_SUPPORT_OCTAL (1 << 2)
        uint8_t tx_nbits_supported;
        uint8_t rx_nbits_supported;
        uint32_t bits_per_word_mask;
#define VIRTIO_SPI_MF_SUPPORT_CPHA_0 (1 << 0)
#define VIRTIO_SPI_MF_SUPPORT_CPHA_1 (1 << 1)
#define VIRTIO_SPI_MF_SUPPORT_CPOL_0 (1 << 2)
#define VIRTIO_SPI_MF_SUPPORT_CPOL_1 (1 << 3)
#define VIRTIO_SPI_MF_SUPPORT_CS_HIGH (1 << 4)
#define VIRTIO_SPI_MF_SUPPORT_LSB_FIRST (1 << 5)
#define VIRTIO_SPI_MF_SUPPORT_LOOPBACK (1 << 6)
        uint32_t mode_func_supported;
        uint32_t max_freq_hz;
        uint32_t max_word_delay_ns;
        uint32_t max_cs_setup_ns;
        uint32_t max_cs_hold_ns;
        uint32_t max_cs_inactive_ns;
};

/*
 * @chip_select_id: chipselect index the SPI transfer used.
 *
 * @bits_per_word: the number of bits in each SPI transfer word.
 *
 * @cs_change: whether to deselect device after finishing this transfer
 *     before starting the next transfer, 0 means cs keep asserted and
 *     1 means cs deasserted then asserted again.
 *
 * @tx_nbits: bus width for write transfer.
 *     0,1: bus width is 1, also known as SINGLE
 *     2  : bus width is 2, also known as DUAL
 *     4  : bus width is 4, also known as QUAD
 *     8  : bus width is 8, also known as OCTAL
 *     other values are invalid.
 *
 * @rx_nbits: bus width for read transfer.
 *     0,1: bus width is 1, also known as SINGLE
 *     2  : bus width is 2, also known as DUAL
 *     4  : bus width is 4, also known as QUAD
 *     8  : bus width is 8, also known as OCTAL
 *     other values are invalid.
 *
 * @reserved: for future use.
 *
 * @mode: SPI transfer mode.
 *     bit 0: CPHA, determines the timing (i.e. phase) of the data
 *         bits relative to the clock pulses.For CPHA=0, the
 *         "out" side changes the data on the trailing edge of the
 *         preceding clock cycle, while the "in" side captures the data
 *         on (or shortly after) the leading edge of the clock cycle.
 *         For CPHA=1, the "out" side changes the data on the leading
 *         edge of the current clock cycle, while the "in" side
 *         captures the data on (or shortly after) the trailing edge of
 *         the clock cycle.
 *     bit 1: CPOL, determines the polarity of the clock. CPOL=0 is a
 *         clock which idles at 0, and each cycle consists of a pulse
 *         of 1. CPOL=1 is a clock which idles at 1, and each cycle
 *         consists of a pulse of 0.
 *     bit 2: CS_HIGH, if 1, chip select active high, else active low.
 *     bit 3: LSB_FIRST, determines per-word bits-on-wire, if 0, MSB
 *         first, else LSB first.
 *     bit 4: LOOP, loopback mode.
 *
 * @freq: the transfer speed in Hz.
 *
 * @word_delay_ns: delay to be inserted between consecutive words of a
 *     transfer, in ns unit.
 *
 * @cs_setup_ns: delay to be introduced after CS is asserted, in ns
 *     unit.
 *
 * @cs_delay_hold_ns: delay to be introduced before CS is deasserted
 *     for each transfer, in ns unit.
 *
 * @cs_change_delay_inactive_ns: delay to be introduced after CS is
 *     deasserted and before next asserted, in ns unit.
 */
struct spi_transfer_head {
        uint8_t chip_select_id;
        uint8_t bits_per_word;
        uint8_t cs_change;
        uint8_t tx_nbits;
        uint8_t rx_nbits;
        uint8_t reserved[3];
        uint32_t mode;
        uint32_t freq;
        uint32_t word_delay_ns;
        uint32_t cs_setup_ns;
        uint32_t cs_delay_hold_ns;
        uint32_t cs_change_delay_inactive_ns;
};

struct spi_transfer_result {
#define VIRTIO_SPI_TRANS_OK 0
#define VIRTIO_SPI_PARAM_ERR 1
#define VIRTIO_SPI_TRANS_ERR 2
	uint8_t status;
};

#endif /* _LINUX_VIRTIO_SPI_H */
