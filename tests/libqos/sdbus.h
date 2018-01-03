/*
 * SD/MMC Bus libqos
 *
 * Copyright (c) 2017 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBQOS_SDBUS_H
#define LIBQOS_SDBUS_H

enum NCmd {
    GO_IDLE_STATE       =  0,
    ALL_SEND_CID        =  2,
    SEND_RELATIVE_ADDR  =  3,
    SELECT_CARD         =  7,
    SEND_IF_COND        =  8,
    SEND_CSD            =  9,
};

enum ACmd {
    SEND_STATUS         = 13,
    SEND_OP_COND        = 41,
    SEND_SCR            = 51,
};

typedef struct SDBusAdapter SDBusAdapter;
struct SDBusAdapter {

    ssize_t (*do_command)(SDBusAdapter *adapter, enum NCmd cmd, uint32_t arg,
                          uint8_t **response);
    void (*write_byte)(SDBusAdapter *adapter, uint8_t value);
    uint8_t (*read_byte)(SDBusAdapter *adapter);
};

ssize_t sdbus_do_cmd(SDBusAdapter *adapter, enum NCmd cmd, uint32_t arg,
                     uint8_t **response);
ssize_t sdbus_do_acmd(SDBusAdapter *adapter, enum ACmd acmd, uint32_t arg,
                      uint16_t address, uint8_t **response);
void sdbus_write_byte(SDBusAdapter *adapter, uint8_t value);
uint8_t sdbus_read_byte(SDBusAdapter *adapter);

SDBusAdapter *qmp_sdbus_create(const char *bus_name);

#endif
