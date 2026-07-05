/*
 * RP2040 single-cycle IO block emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RP2040_SIO_H
#define HW_MISC_RP2040_SIO_H

#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_RP2040_SIO "rp2040-sio"
OBJECT_DECLARE_SIMPLE_TYPE(RP2040SioState, RP2040_SIO)

#define RP2040_SIO_BASE 0xd0000000
#define RP2040_SIO_NUM_CORES 2
#define RP2040_SIO_FIFO_DEPTH 8
#define RP2040_SIO_NUM_INTERPS 2
#define RP2040_SIO_INTERP_NUM_LANES 2
#define RP2040_SIO_INTERP_NUM_BASES 3

struct RP2040SioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq fifo_irq[RP2040_SIO_NUM_CORES];
    uint32_t gpio_in;
    uint32_t gpio_hi_in;
    uint32_t gpio_out;
    uint32_t gpio_oe;
    uint32_t gpio_hi_out;
    uint32_t gpio_hi_oe;
    uint32_t fifo[RP2040_SIO_NUM_CORES][RP2040_SIO_FIFO_DEPTH];
    uint8_t fifo_rptr[RP2040_SIO_NUM_CORES];
    uint8_t fifo_wptr[RP2040_SIO_NUM_CORES];
    uint8_t fifo_level[RP2040_SIO_NUM_CORES];
    uint32_t fifo_sticky[RP2040_SIO_NUM_CORES];
    uint32_t div_dividend[RP2040_SIO_NUM_CORES];
    uint32_t div_divisor[RP2040_SIO_NUM_CORES];
    uint32_t div_quotient[RP2040_SIO_NUM_CORES];
    uint32_t div_remainder[RP2040_SIO_NUM_CORES];
    bool div_dirty[RP2040_SIO_NUM_CORES];
    uint32_t interp_accum[RP2040_SIO_NUM_CORES]
                          [RP2040_SIO_NUM_INTERPS *
                           RP2040_SIO_INTERP_NUM_LANES];
    uint32_t interp_base[RP2040_SIO_NUM_CORES]
                         [RP2040_SIO_NUM_INTERPS *
                          RP2040_SIO_INTERP_NUM_BASES];
    uint32_t interp_ctrl[RP2040_SIO_NUM_CORES]
                         [RP2040_SIO_NUM_INTERPS *
                          RP2040_SIO_INTERP_NUM_LANES];
    uint32_t spinlock_st;
};

#endif
