/*
 * SD/MMC cards common helpers
 *
 * Copyright (c) 2018  Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "sdmmc-internal.h"

const char *sd_cmd_name(uint8_t cmd)
{
    static const char *cmd_abbrev[SDMMC_CMD_MAX] = {
         [0]    = "GO_IDLE_STATE",
         [2]    = "ALL_SEND_CID",            [3]    = "SEND_RELATIVE_ADDR",
         [4]    = "SET_DSR",                 [5]    = "IO_SEND_OP_COND",
         [6]    = "SWITCH_FUNC",             [7]    = "SELECT/DESELECT_CARD",
         [8]    = "SEND_IF_COND",            [9]    = "SEND_CSD",
        [10]    = "SEND_CID",               [11]    = "VOLTAGE_SWITCH",
        [12]    = "STOP_TRANSMISSION",      [13]    = "SEND_STATUS",
                                            [15]    = "GO_INACTIVE_STATE",
        [16]    = "SET_BLOCKLEN",           [17]    = "READ_SINGLE_BLOCK",
        [18]    = "READ_MULTIPLE_BLOCK",    [19]    = "SEND_TUNING_BLOCK",
        [20]    = "SPEED_CLASS_CONTROL",    [21]    = "DPS_spec",
                                            [23]    = "SET_BLOCK_COUNT",
        [24]    = "WRITE_BLOCK",            [25]    = "WRITE_MULTIPLE_BLOCK",
        [26]    = "MANUF_RSVD",             [27]    = "PROGRAM_CSD",
        [28]    = "SET_WRITE_PROT",         [29]    = "CLR_WRITE_PROT",
        [30]    = "SEND_WRITE_PROT",
        [32]    = "ERASE_WR_BLK_START",     [33]    = "ERASE_WR_BLK_END",
        [34]    = "SW_FUNC_RSVD",           [35]    = "SW_FUNC_RSVD",
        [36]    = "SW_FUNC_RSVD",           [37]    = "SW_FUNC_RSVD",
        [38]    = "ERASE",
        [40]    = "DPS_spec",
        [42]    = "LOCK_UNLOCK",            [43]    = "Q_MANAGEMENT",
        [44]    = "Q_TASK_INFO_A",          [45]    = "Q_TASK_INFO_B",
        [46]    = "Q_RD_TASK",              [47]    = "Q_WR_TASK",
        [48]    = "READ_EXTR_SINGLE",       [49]    = "WRITE_EXTR_SINGLE",
        [50]    = "SW_FUNC_RSVD",
        [52]    = "IO_RW_DIRECT",           [53]    = "IO_RW_EXTENDED",
        [54]    = "SDIO_RSVD",              [55]    = "APP_CMD",
        [56]    = "GEN_CMD",                [57]    = "SW_FUNC_RSVD",
        [58]    = "READ_EXTR_MULTI",        [59]    = "WRITE_EXTR_MULTI",
        [60]    = "MANUF_RSVD",             [61]    = "MANUF_RSVD",
        [62]    = "MANUF_RSVD",             [63]    = "MANUF_RSVD",
    };
    return cmd_abbrev[cmd] ? cmd_abbrev[cmd] : "UNKNOWN_CMD";
}

const char *sd_acmd_name(uint8_t cmd)
{
    static const char *acmd_abbrev[SDMMC_CMD_MAX] = {
         [6] = "SET_BUS_WIDTH",
        [13] = "SD_STATUS",
        [14] = "DPS_spec",                  [15] = "DPS_spec",
        [16] = "DPS_spec",
        [18] = "SECU_spec",
        [22] = "SEND_NUM_WR_BLOCKS",        [23] = "SET_WR_BLK_ERASE_COUNT",
        [41] = "SD_SEND_OP_COND",
        [42] = "SET_CLR_CARD_DETECT",
        [51] = "SEND_SCR",
        [52] = "SECU_spec",                 [53] = "SECU_spec",
        [54] = "SECU_spec",
        [56] = "SECU_spec",                 [57] = "SECU_spec",
        [58] = "SECU_spec",                 [59] = "SECU_spec",
    };

    return acmd_abbrev[cmd] ? acmd_abbrev[cmd] : "UNKNOWN_ACMD";
}

/* 7 bit CRC with polynomial x^7 + x^3 + 1 */
uint8_t sd_crc7(const void *message, size_t width)
{
    int i, bit;
    uint8_t shift_reg = 0x00;
    const uint8_t *msg = (const uint8_t *)message;

    for (i = 0; i < width; i++, msg++) {
        for (bit = 7; bit >= 0; bit--) {
            shift_reg <<= 1;
            if ((shift_reg >> 7) ^ ((*msg >> bit) & 1)) {
                shift_reg ^= 0x89;
            }
        }
    }

    return shift_reg;
}

uint16_t sd_crc16(const void *message, size_t width)
{
    int i, bit;
    uint16_t shift_reg = 0x0000;
    const uint16_t *msg = (const uint16_t *)message;
    width <<= 1;

    for (i = 0; i < width; i++, msg++) {
        for (bit = 15; bit >= 0; bit--) {
            shift_reg <<= 1;
            if ((shift_reg >> 15) ^ ((*msg >> bit) & 1)) {
                shift_reg ^= 0x1011;
            }
        }
    }

    return shift_reg;
}

static uint8_t sd_calc_frame48_crc7(uint8_t cmd, uint32_t arg)
{
    uint8_t buffer[5];
    buffer[0] = 0x40 | cmd;
    stl_be_p(&buffer[1], arg);
    return sd_crc7(buffer, sizeof(buffer));
}

bool sd_verify_frame48_checksum(SDFrame48 *frame)
{
    uint8_t crc = sd_calc_frame48_crc7(frame->cmd, frame->arg);

    return crc == frame->crc;
}

void sd_update_frame48_checksum(SDFrame48 *frame)
{
    frame->crc = sd_calc_frame48_crc7(frame->cmd, frame->arg);
}

static void sd_prepare_frame48(SDFrame48 *frame, uint8_t cmd, uint32_t arg,
                               bool gen_crc)
{
    frame->cmd = cmd;
    frame->arg = arg;
    frame->crc = 0x00;
    if (gen_crc) {
        sd_update_frame48_checksum(frame);
    }
}

void sd_prepare_request(SDFrame48 *req, uint8_t cmd, uint32_t arg, bool gen_crc)
{
    sd_prepare_frame48(req, cmd, arg, gen_crc);
}

void sd_prepare_request_with_crc(SDRequest *req, uint8_t cmd, uint32_t arg,
                                 uint8_t crc)
{
    sd_prepare_frame48(req, cmd, arg, /* gen_crc */ false);
    req->crc = crc;
}
