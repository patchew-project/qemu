/*
 * Andes PLIC (Platform Level Interrupt Controller) interface
 *
 * Copyright (c) 2018 Andes Tech. Corp.
 *
 * This provides a RISC-V PLIC device with Andes' extensions.
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

#ifndef HW_ANDES_PLIC_H
#define HW_ANDES_PLIC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define ANDES_AE350_PLIC_NAME             "ANDES_PLIC"
#define ANDES_AE350_PLIC_HART_CONFIG      "MS"
#define ANDES_AE350_PLIC_NUM_SOURCES      128
#define ANDES_AE350_PLIC_NUM_PRIORITIES   32
#define ANDES_AE350_PLIC_PRIORITY_BASE    0x04
#define ANDES_AE350_PLIC_PENDING_BASE     0x1000
#define ANDES_AE350_PLIC_ENABLE_BASE      0x2000
#define ANDES_AE350_PLIC_ENABLE_STRIDE    0x80
#define ANDES_AE350_PLIC_THRESHOLD_BASE   0x200000
#define ANDES_AE350_PLIC_THRESHOLD_STRIDE 0x1000

#define ANDES_AE350_PLICSW_NAME           "ANDES_PLICSW"
#define ANDES_AE350_PLICSW_HART_CONFIG    "M"
#define ANDES_AE350_PLICSW_NUM_SOURCES    64
#define ANDES_AE350_PLICSW_NUM_PRIORITIES 8
#define ANDES_AE350_PLICSW_PRIORITY_BASE  0x4
#define ANDES_AE350_PLICSW_PENDING_BASE   0x1000
#define ANDES_AE350_PLICSW_ENABLE_BASE    0x2000
#define ANDES_AE350_PLICSW_ENABLE_STRIDE  0x80
#define ANDES_AE350_PLICSW_THRESHOLD_BASE   0x200000
#define ANDES_AE350_PLICSW_THRESHOLD_STRIDE 0x1000

#define TYPE_ANDES_PLIC "riscv.andes.plic"

typedef struct AndesPLICState AndesPLICState;
DECLARE_INSTANCE_CHECKER(AndesPLICState, ANDES_PLIC,
                         TYPE_ANDES_PLIC)

typedef enum AndesPLICMode {
    PlicMode_U,
    PlicMode_S,
    PlicMode_H,
    PlicMode_M
} AndesPLICMode;

typedef struct AndesPLICAddr {
    uint32_t target_id;
    uint32_t hart_id;
    AndesPLICMode mode;
} AndesPLICAddr;

typedef struct AndesPLICState {
    /*< private >*/
    SysBusDevice parent_mmio;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t num_addrs;
    uint32_t num_harts;
    uint32_t bitfield_words;
    uint32_t num_enables;
    AndesPLICAddr *addr_config;
    uint32_t *source_priority;
    uint32_t *target_priority;
    uint32_t *pending;
    uint32_t *claimed;
    uint32_t *enable;

    /* config */
    char *hart_config;
    char *plic_name;
    uint32_t plic_base;
    uint32_t hartid_base;
    uint32_t num_sources;
    uint32_t num_priorities;
    uint32_t priority_base;
    uint32_t pending_base;
    uint32_t enable_base;
    uint32_t enable_stride;
    uint32_t threshold_base;
    uint32_t threshold_stride;
    uint32_t aperture_size;

    /* interface */
    void (*andes_plic_update)(AndesPLICState *plic);
} AndesPLICState;

void andes_plichw_update(AndesPLICState *plic);
void andes_plicsw_update(AndesPLICState *plic);

static inline bool addr_between(uint32_t addr, uint32_t base, uint32_t offset)
{
    return (addr >= base && addr < base + offset);
}

DeviceState *
andes_plic_create(hwaddr addr,
    const char *plic_name, char *hart_config,
    uint32_t num_sources, uint32_t num_priorities,
    uint32_t priority_base, uint32_t pending_base,
    uint32_t enable_base, uint32_t enable_stride,
    uint32_t threshold_base, uint32_t threshold_stride,
    uint32_t aperture_size);

DeviceState *
andes_plicsw_create(hwaddr addr,
    const char *plic_name, char *hart_config,
    uint32_t num_sources, uint32_t num_priorities,
    uint32_t priority_base, uint32_t pending_base,
    uint32_t enable_base, uint32_t enable_stride,
    uint32_t threshold_base, uint32_t threshold_stride,
    uint32_t aperture_size);

#endif
