/*
 * Allwinner H3 SD Host Controller emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ALLWINNER_H3_SDHOST_H
#define ALLWINNER_H3_SDHOST_H

#include "hw/sysbus.h"
#include "hw/sd/sd.h"

#define AW_H3_SDHOST_REGS_MEM_SIZE  (1024)

#define TYPE_AW_H3_SDHOST "allwinner-h3-sdhost"
#define AW_H3_SDHOST(obj) \
        OBJECT_CHECK(AwH3SDHostState, (obj), TYPE_AW_H3_SDHOST)

typedef struct {
    SysBusDevice busdev;
    SDBus sdbus;
    MemoryRegion iomem;

    uint32_t global_ctl;
    uint32_t clock_ctl;
    uint32_t timeout;
    uint32_t bus_width;
    uint32_t block_size;
    uint32_t byte_count;
    uint32_t transfer_cnt;

    uint32_t command;
    uint32_t command_arg;
    uint32_t response[4];

    uint32_t irq_mask;
    uint32_t irq_status;
    uint32_t status;

    uint32_t fifo_wlevel;
    uint32_t fifo_func_sel;
    uint32_t debug_enable;
    uint32_t auto12_arg;
    uint32_t newtiming_set;
    uint32_t newtiming_debug;
    uint32_t hardware_rst;
    uint32_t dmac;
    uint32_t desc_base;
    uint32_t dmac_status;
    uint32_t dmac_irq;
    uint32_t card_threshold;
    uint32_t startbit_detect;
    uint32_t response_crc;
    uint32_t data_crc[8];
    uint32_t status_crc;

    qemu_irq irq;
} AwH3SDHostState;

#endif
