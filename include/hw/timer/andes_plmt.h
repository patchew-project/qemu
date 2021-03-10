/*
 * Andes PLMT (Platform Level Machine Timer) interface
 *
 * Copyright (c) 2021 Andes Tech. Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ANDES_PLMT_H
#define HW_ANDES_PLMT_H

#define TYPE_ANDES_PLMT "riscv.andes.plmt"

#define ANDES_PLMT(obj) \
    OBJECT_CHECK(AndesPLMTState, (obj), TYPE_ANDES_PLMT)

typedef struct AndesPLMTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t num_harts;
    uint32_t time_base;
    uint32_t timecmp_base;
    uint32_t aperture_size;
} AndesPLMTState;

DeviceState *
andes_plmt_create(hwaddr addr, hwaddr size, uint32_t num_harts,
    uint32_t time_base, uint32_t timecmp_base);

enum {
    ANDES_PLMT_TIME_BASE = 0,
    ANDES_PLMT_TIMECMP_BASE = 8,
    ANDES_PLMT_MMIO_SIZE = 0x100000,
    ANDES_PLMT_TIMEBASE_FREQ = 60 * 1000 * 1000
};

#endif
