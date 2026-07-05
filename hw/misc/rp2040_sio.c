/*
 * RP2040 single-cycle IO block emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_sio.h"
#include "hw/core/cpu.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define SIO_CPUID               0x000
#define SIO_GPIO_IN             0x004
#define SIO_GPIO_HI_IN          0x008
#define SIO_GPIO_OUT            0x010
#define SIO_GPIO_OUT_SET        0x014
#define SIO_GPIO_OUT_CLR        0x018
#define SIO_GPIO_OUT_XOR        0x01c
#define SIO_GPIO_OE             0x020
#define SIO_GPIO_OE_SET         0x024
#define SIO_GPIO_OE_CLR         0x028
#define SIO_GPIO_OE_XOR         0x02c
#define SIO_GPIO_HI_OUT         0x030
#define SIO_GPIO_HI_OUT_SET     0x034
#define SIO_GPIO_HI_OUT_CLR     0x038
#define SIO_GPIO_HI_OUT_XOR     0x03c
#define SIO_GPIO_HI_OE          0x040
#define SIO_GPIO_HI_OE_SET      0x044
#define SIO_GPIO_HI_OE_CLR      0x048
#define SIO_GPIO_HI_OE_XOR      0x04c
#define SIO_FIFO_ST             0x050
#define SIO_FIFO_WR             0x054
#define SIO_FIFO_RD             0x058
#define SIO_SPINLOCK_ST         0x05c
#define SIO_DIV_UDIVIDEND       0x060
#define SIO_DIV_UDIVISOR        0x064
#define SIO_DIV_SDIVIDEND       0x068
#define SIO_DIV_SDIVISOR        0x06c
#define SIO_DIV_QUOTIENT        0x070
#define SIO_DIV_REMAINDER       0x074
#define SIO_DIV_CSR             0x078
#define SIO_INTERP0_BASE        0x080
#define SIO_INTERP1_BASE        0x0c0
#define SIO_INTERP_BLOCK_SIZE   0x040
#define SIO_INTERP_ACCUM0       0x00
#define SIO_INTERP_ACCUM1       0x04
#define SIO_INTERP_BASE0        0x08
#define SIO_INTERP_BASE1        0x0c
#define SIO_INTERP_BASE2        0x10
#define SIO_INTERP_POP_LANE0    0x14
#define SIO_INTERP_POP_LANE1    0x18
#define SIO_INTERP_POP_FULL     0x1c
#define SIO_INTERP_PEEK_LANE0   0x20
#define SIO_INTERP_PEEK_LANE1   0x24
#define SIO_INTERP_PEEK_FULL    0x28
#define SIO_INTERP_CTRL_LANE0   0x2c
#define SIO_INTERP_CTRL_LANE1   0x30
#define SIO_INTERP_ACCUM0_ADD   0x34
#define SIO_INTERP_ACCUM1_ADD   0x38
#define SIO_INTERP_BASE_1AND0   0x3c
#define SIO_SPINLOCK_BASE       0x100
#define SIO_SPINLOCK_LAST       0x17c

#define SIO_GPIO_MASK           0x3fffffff
#define SIO_GPIO_HI_MASK        0x3f
#define SIO_FIFO_ST_VLD         BIT(0)
#define SIO_FIFO_ST_RDY         BIT(1)
#define SIO_FIFO_ST_WC_MASK     (BIT(3) | BIT(2))
#define SIO_DIV_CSR_READY       BIT(0)
#define SIO_DIV_CSR_DIRTY       BIT(1)
#define SIO_INTERP_CTRL_FORCE_MSB_SHIFT 19
#define SIO_INTERP_CTRL_ADD_RAW         BIT(18)
#define SIO_INTERP_CTRL_CROSS_RESULT    BIT(17)
#define SIO_INTERP_CTRL_CROSS_INPUT     BIT(16)
#define SIO_INTERP_CTRL_SIGNED          BIT(15)
#define SIO_INTERP_CTRL_MASK_MSB_SHIFT  10
#define SIO_INTERP_CTRL_MASK_LSB_SHIFT  5
#define SIO_INTERP_CTRL_SHIFT_MASK      0x1f
#define SIO_INTERP_CTRL_LSB_MASK        (0x1f << 5)
#define SIO_INTERP_CTRL_MSB_MASK        (0x1f << 10)
#define SIO_INTERP0_CTRL_BLEND          BIT(21)
#define SIO_INTERP1_CTRL_CLAMP          BIT(22)
#define SIO_INTERP_CTRL_OVERF           BIT(25)
#define SIO_INTERP_CTRL_OVERF1          BIT(24)
#define SIO_INTERP_CTRL_OVERF0          BIT(23)
#define SIO_INTERP_CTRL_OVERF_MASK      (SIO_INTERP_CTRL_OVERF | \
                                         SIO_INTERP_CTRL_OVERF1 | \
                                         SIO_INTERP_CTRL_OVERF0)

typedef struct RP2040SioInterpResult {
    uint32_t raw[RP2040_SIO_INTERP_NUM_LANES];
    uint32_t lane[RP2040_SIO_INTERP_NUM_LANES];
    uint32_t full;
} RP2040SioInterpResult;

static unsigned rp2040_sio_current_core(void)
{
    /*
     * current_cpu is QEMU's thread-local pointer to the currently executing
     * guest vCPU, not a host CPU identifier.
     */
    if (current_cpu && current_cpu->cpu_index == 1) {
        return 1;
    }

    return 0;
}

static void rp2040_sio_update_fifo_irq(RP2040SioState *s)
{
    int i;

    for (i = 0; i < RP2040_SIO_NUM_CORES; i++) {
        qemu_set_irq(s->fifo_irq[i], s->fifo_level[i] != 0);
    }
}

static uint32_t rp2040_sio_fifo_status(RP2040SioState *s, unsigned core)
{
    unsigned peer = core ^ 1;
    uint32_t value = s->fifo_sticky[core] & SIO_FIFO_ST_WC_MASK;

    if (s->fifo_level[core] != 0) {
        value |= SIO_FIFO_ST_VLD;
    }
    if (s->fifo_level[peer] < RP2040_SIO_FIFO_DEPTH) {
        value |= SIO_FIFO_ST_RDY;
    }

    return value;
}

static void rp2040_sio_fifo_push(RP2040SioState *s, unsigned core,
                                 uint32_t value)
{
    unsigned peer = core ^ 1;

    if (s->fifo_level[peer] == RP2040_SIO_FIFO_DEPTH) {
        s->fifo_sticky[core] |= BIT(2);
        return;
    }

    s->fifo[peer][s->fifo_wptr[peer]] = value;
    s->fifo_wptr[peer] = (s->fifo_wptr[peer] + 1) % RP2040_SIO_FIFO_DEPTH;
    s->fifo_level[peer]++;
    rp2040_sio_update_fifo_irq(s);
}

static uint32_t rp2040_sio_fifo_pop(RP2040SioState *s, unsigned core)
{
    uint32_t value;

    if (s->fifo_level[core] == 0) {
        s->fifo_sticky[core] |= BIT(3);
        return 0;
    }

    value = s->fifo[core][s->fifo_rptr[core]];
    s->fifo_rptr[core] = (s->fifo_rptr[core] + 1) % RP2040_SIO_FIFO_DEPTH;
    s->fifo_level[core]--;
    rp2040_sio_update_fifo_irq(s);

    return value;
}

static bool rp2040_sio_spinlock_offset(hwaddr offset, unsigned *index)
{
    if (offset < SIO_SPINLOCK_BASE || offset > SIO_SPINLOCK_LAST ||
        (offset & 0x3)) {
        return false;
    }

    *index = (offset - SIO_SPINLOCK_BASE) / sizeof(uint32_t);
    return *index < 32;
}

static int32_t rp2040_sio_sign32(uint32_t value)
{
    int32_t signed_value = (int32_t)value;

    if (signed_value > 0) {
        return 1;
    }
    if (signed_value < 0) {
        return -1;
    }
    return 0;
}

static void rp2040_sio_divide(RP2040SioState *s, unsigned core,
                              bool is_signed)
{
    uint32_t dividend = s->div_dividend[core];
    uint32_t divisor = s->div_divisor[core];
    uint32_t quotient;
    uint32_t remainder;

    if (is_signed) {
        int32_t signed_dividend = (int32_t)dividend;
        int32_t signed_divisor = (int32_t)divisor;

        if (signed_divisor == 0) {
            quotient = -rp2040_sio_sign32(dividend);
            remainder = dividend;
        } else if (signed_dividend == INT32_MIN && signed_divisor == -1) {
            quotient = dividend;
            remainder = 0;
        } else {
            quotient = signed_dividend / signed_divisor;
            remainder = signed_dividend % signed_divisor;
        }
    } else if (divisor == 0) {
        quotient = UINT32_MAX;
        remainder = dividend;
    } else {
        quotient = dividend / divisor;
        remainder = dividend % divisor;
    }

    s->div_quotient[core] = quotient;
    s->div_remainder[core] = remainder;
    s->div_dirty[core] = true;
}

static uint32_t rp2040_sio_div_csr(RP2040SioState *s, unsigned core)
{
    return SIO_DIV_CSR_READY |
           (s->div_dirty[core] ? SIO_DIV_CSR_DIRTY : 0);
}

static unsigned rp2040_sio_interp_index(unsigned interp, unsigned lane)
{
    return interp * RP2040_SIO_INTERP_NUM_LANES + lane;
}

static unsigned rp2040_sio_interp_base_index(unsigned interp, unsigned base)
{
    return interp * RP2040_SIO_INTERP_NUM_BASES + base;
}

static uint32_t rp2040_sio_interp_ctrl_mask(unsigned interp, unsigned lane)
{
    uint32_t mask = SIO_INTERP_CTRL_SHIFT_MASK |
                    SIO_INTERP_CTRL_LSB_MASK |
                    SIO_INTERP_CTRL_MSB_MASK |
                    SIO_INTERP_CTRL_SIGNED |
                    SIO_INTERP_CTRL_CROSS_INPUT |
                    SIO_INTERP_CTRL_CROSS_RESULT |
                    SIO_INTERP_CTRL_ADD_RAW |
                    (0x3 << SIO_INTERP_CTRL_FORCE_MSB_SHIFT);

    if (interp == 0 && lane == 0) {
        mask |= SIO_INTERP0_CTRL_BLEND;
    } else if (interp == 1 && lane == 0) {
        mask |= SIO_INTERP1_CTRL_CLAMP;
    }

    return mask;
}

static bool rp2040_sio_interp_offset(hwaddr addr, unsigned *interp,
                                     hwaddr *reg)
{
    if (addr >= SIO_INTERP0_BASE &&
        addr < SIO_INTERP0_BASE + SIO_INTERP_BLOCK_SIZE) {
        *interp = 0;
        *reg = addr - SIO_INTERP0_BASE;
        return true;
    }

    if (addr >= SIO_INTERP1_BASE &&
        addr < SIO_INTERP1_BASE + SIO_INTERP_BLOCK_SIZE) {
        *interp = 1;
        *reg = addr - SIO_INTERP1_BASE;
        return true;
    }

    return false;
}

static uint32_t rp2040_sio_interp_mask(unsigned lsb, unsigned msb)
{
    if (msb < lsb) {
        return 0;
    }

    if (msb == 31) {
        return UINT32_MAX << lsb;
    }

    return (BIT(msb + 1) - 1) & ~(BIT(lsb) - 1);
}

static uint32_t rp2040_sio_interp_sign_extend(uint32_t value, unsigned msb)
{
    if (msb == 31 || !(value & BIT(msb))) {
        return value;
    }

    return value | (UINT32_MAX << (msb + 1));
}

static uint32_t rp2040_sio_interp_raw(RP2040SioState *s, unsigned core,
                                      unsigned interp, unsigned lane)
{
    uint32_t ctrl = s->interp_ctrl[core]
                                  [rp2040_sio_interp_index(interp, lane)];
    uint32_t input;
    uint32_t shifted;
    uint32_t value;
    unsigned shift = ctrl & SIO_INTERP_CTRL_SHIFT_MASK;
    unsigned lsb = (ctrl & SIO_INTERP_CTRL_LSB_MASK) >>
                   SIO_INTERP_CTRL_MASK_LSB_SHIFT;
    unsigned msb = (ctrl & SIO_INTERP_CTRL_MSB_MASK) >>
                   SIO_INTERP_CTRL_MASK_MSB_SHIFT;

    input = s->interp_accum[core][rp2040_sio_interp_index(
                                  interp,
                                  (ctrl & SIO_INTERP_CTRL_CROSS_INPUT) ?
                                  (lane ^ 1) : lane)];
    if (ctrl & SIO_INTERP_CTRL_ADD_RAW) {
        return input;
    }

    shifted = input >> shift;
    value = shifted & rp2040_sio_interp_mask(lsb, msb);
    if (ctrl & SIO_INTERP_CTRL_SIGNED) {
        value = rp2040_sio_interp_sign_extend(value, msb);
    }

    return value;
}

static uint32_t rp2040_sio_interp_force_msb(RP2040SioState *s, unsigned core,
                                            unsigned interp, unsigned lane,
                                            uint32_t value)
{
    uint32_t ctrl = s->interp_ctrl[core]
                                  [rp2040_sio_interp_index(interp, lane)];
    uint32_t force = (ctrl >> SIO_INTERP_CTRL_FORCE_MSB_SHIFT) & 0x3;

    return value | (force << 28);
}

static uint32_t rp2040_sio_interp_blend_result(RP2040SioState *s,
                                               unsigned core, uint32_t alpha)
{
    uint32_t ctrl1 = s->interp_ctrl[core][rp2040_sio_interp_index(0, 1)];
    uint32_t base0 = s->interp_base[core][rp2040_sio_interp_base_index(0, 0)];
    uint32_t base1 = s->interp_base[core][rp2040_sio_interp_base_index(0, 1)];
    int64_t start;
    int64_t end;

    if (ctrl1 & SIO_INTERP_CTRL_SIGNED) {
        start = (int16_t)base0;
        end = (int16_t)base1;
    } else {
        start = (uint16_t)base0;
        end = (uint16_t)base1;
    }

    return start + (((end - start) * (alpha & 0xff)) >> 8);
}

static void rp2040_sio_interp_compute(RP2040SioState *s, unsigned core,
                                      unsigned interp,
                                      RP2040SioInterpResult *result)
{
    bool blend = interp == 0 &&
                 (s->interp_ctrl[core][rp2040_sio_interp_index(0, 0)] &
                  SIO_INTERP0_CTRL_BLEND);
    bool clamp = interp == 1 &&
                 (s->interp_ctrl[core][rp2040_sio_interp_index(1, 0)] &
                  SIO_INTERP1_CTRL_CLAMP);
    uint32_t base0 = s->interp_base[core]
                                   [rp2040_sio_interp_base_index(interp, 0)];
    uint32_t base1 = s->interp_base[core]
                                   [rp2040_sio_interp_base_index(interp, 1)];
    uint32_t base2 = s->interp_base[core]
                                   [rp2040_sio_interp_base_index(interp, 2)];

    result->raw[0] = rp2040_sio_interp_raw(s, core, interp, 0);
    result->raw[1] = rp2040_sio_interp_raw(s, core, interp, 1);
    result->lane[0] = base0 + result->raw[0];
    result->lane[1] = base1 + result->raw[1];
    result->full = base2 + result->raw[0] + result->raw[1];

    if (blend) {
        uint32_t alpha = result->raw[1] & 0xff;

        result->lane[0] = alpha;
        result->lane[1] = rp2040_sio_interp_blend_result(s, core, alpha);
        result->full = base2 + result->raw[0];
    } else if (clamp) {
        uint32_t ctrl0 = s->interp_ctrl[core][rp2040_sio_interp_index(1, 0)];

        if (ctrl0 & SIO_INTERP_CTRL_SIGNED) {
            int32_t value = result->raw[0];
            int32_t lower = base0;
            int32_t upper = base1;

            result->lane[0] = value < lower ? lower :
                              value > upper ? upper : value;
        } else {
            result->lane[0] = result->raw[0] < base0 ? base0 :
                              result->raw[0] > base1 ? base1 :
                              result->raw[0];
        }
    }
}

static uint32_t rp2040_sio_interp_pop(RP2040SioState *s, unsigned core,
                                      unsigned interp, unsigned lane)
{
    RP2040SioInterpResult result;
    uint32_t ctrl0 = s->interp_ctrl[core]
                                   [rp2040_sio_interp_index(interp, 0)];
    uint32_t ctrl1 = s->interp_ctrl[core]
                                   [rp2040_sio_interp_index(interp, 1)];
    uint32_t next0;
    uint32_t next1;

    rp2040_sio_interp_compute(s, core, interp, &result);
    next0 = (ctrl0 & SIO_INTERP_CTRL_CROSS_RESULT) ?
            result.lane[1] : result.lane[0];
    next1 = (ctrl1 & SIO_INTERP_CTRL_CROSS_RESULT) ?
            result.lane[0] : result.lane[1];

    s->interp_accum[core][rp2040_sio_interp_index(interp, 0)] = next0;
    s->interp_accum[core][rp2040_sio_interp_index(interp, 1)] = next1;

    return lane < 2 ? rp2040_sio_interp_force_msb(s, core, interp, lane,
                                                  result.lane[lane]) :
                      result.full;
}

static uint32_t rp2040_sio_interp_overf(RP2040SioState *s, unsigned core,
                                        unsigned interp, unsigned lane)
{
    uint32_t ctrl = s->interp_ctrl[core]
                                  [rp2040_sio_interp_index(interp, lane)];
    unsigned shift = ctrl & SIO_INTERP_CTRL_SHIFT_MASK;
    unsigned lsb = (ctrl & SIO_INTERP_CTRL_LSB_MASK) >>
                   SIO_INTERP_CTRL_MASK_LSB_SHIFT;
    unsigned msb = (ctrl & SIO_INTERP_CTRL_MSB_MASK) >>
                   SIO_INTERP_CTRL_MASK_MSB_SHIFT;
    uint32_t input = s->interp_accum[core][rp2040_sio_interp_index(
                                           interp,
                                           (ctrl &
                                            SIO_INTERP_CTRL_CROSS_INPUT) ?
                                           (lane ^ 1) : lane)];
    uint32_t shifted = input >> shift;

    return (shifted & ~rp2040_sio_interp_mask(lsb, msb)) != 0;
}

static uint32_t rp2040_sio_interp_ctrl_read(RP2040SioState *s, unsigned core,
                                            unsigned interp, unsigned lane)
{
    uint32_t ctrl = s->interp_ctrl[core]
                                  [rp2040_sio_interp_index(interp, lane)];
    bool overf0 = rp2040_sio_interp_overf(s, core, interp, 0);
    bool overf1 = rp2040_sio_interp_overf(s, core, interp, 1);

    ctrl &= ~SIO_INTERP_CTRL_OVERF_MASK;
    if (overf0) {
        ctrl |= SIO_INTERP_CTRL_OVERF0;
    }
    if (overf1) {
        ctrl |= SIO_INTERP_CTRL_OVERF1;
    }
    if (overf0 || overf1) {
        ctrl |= SIO_INTERP_CTRL_OVERF;
    }

    return ctrl;
}

static uint32_t rp2040_sio_interp_read(RP2040SioState *s, unsigned core,
                                       unsigned interp, hwaddr reg)
{
    RP2040SioInterpResult result;

    switch (reg) {
    case SIO_INTERP_ACCUM0:
    case SIO_INTERP_ACCUM1:
        return s->interp_accum[core][rp2040_sio_interp_index(
                                     interp, reg == SIO_INTERP_ACCUM1)];
    case SIO_INTERP_BASE0:
    case SIO_INTERP_BASE1:
    case SIO_INTERP_BASE2:
        return s->interp_base[core][rp2040_sio_interp_base_index(
                                  interp, (reg - SIO_INTERP_BASE0) /
                                          sizeof(uint32_t))];
    case SIO_INTERP_POP_LANE0:
        return rp2040_sio_interp_pop(s, core, interp, 0);
    case SIO_INTERP_POP_LANE1:
        return rp2040_sio_interp_pop(s, core, interp, 1);
    case SIO_INTERP_POP_FULL:
        return rp2040_sio_interp_pop(s, core, interp, 2);
    case SIO_INTERP_PEEK_LANE0:
    case SIO_INTERP_PEEK_LANE1:
    case SIO_INTERP_PEEK_FULL:
        rp2040_sio_interp_compute(s, core, interp, &result);
        if (reg == SIO_INTERP_PEEK_LANE0) {
            return rp2040_sio_interp_force_msb(s, core, interp, 0,
                                               result.lane[0]);
        }
        if (reg == SIO_INTERP_PEEK_LANE1) {
            return rp2040_sio_interp_force_msb(s, core, interp, 1,
                                               result.lane[1]);
        }
        return result.full;
    case SIO_INTERP_CTRL_LANE0:
    case SIO_INTERP_CTRL_LANE1:
        return rp2040_sio_interp_ctrl_read(s, core, interp,
                                           reg == SIO_INTERP_CTRL_LANE1);
    case SIO_INTERP_ACCUM0_ADD:
    case SIO_INTERP_ACCUM1_ADD:
        return rp2040_sio_interp_raw(s, core, interp,
                                     reg == SIO_INTERP_ACCUM1_ADD);
    case SIO_INTERP_BASE_1AND0:
        return 0;
    default:
        return 0;
    }
}

static void rp2040_sio_interp_write(RP2040SioState *s, unsigned core,
                                    unsigned interp, hwaddr reg,
                                    uint32_t value)
{
    unsigned lane;
    unsigned base;
    bool base01_signed;

    switch (reg) {
    case SIO_INTERP_ACCUM0:
    case SIO_INTERP_ACCUM1:
        lane = reg == SIO_INTERP_ACCUM1;
        s->interp_accum[core][rp2040_sio_interp_index(interp, lane)] = value;
        break;
    case SIO_INTERP_BASE0:
    case SIO_INTERP_BASE1:
    case SIO_INTERP_BASE2:
        base = (reg - SIO_INTERP_BASE0) / sizeof(uint32_t);
        s->interp_base[core][rp2040_sio_interp_base_index(interp, base)] =
            value;
        break;
    case SIO_INTERP_CTRL_LANE0:
    case SIO_INTERP_CTRL_LANE1:
        lane = reg == SIO_INTERP_CTRL_LANE1;
        s->interp_ctrl[core][rp2040_sio_interp_index(interp, lane)] =
            value & rp2040_sio_interp_ctrl_mask(interp, lane);
        break;
    case SIO_INTERP_ACCUM0_ADD:
    case SIO_INTERP_ACCUM1_ADD:
        lane = reg == SIO_INTERP_ACCUM1_ADD;
        s->interp_accum[core][rp2040_sio_interp_index(interp, lane)] +=
            value & 0x00ffffff;
        break;
    case SIO_INTERP_BASE_1AND0:
        base01_signed = interp == 0 &&
                        (s->interp_ctrl[core]
                                       [rp2040_sio_interp_index(0, 0)] &
                         SIO_INTERP0_CTRL_BLEND) ?
                        (s->interp_ctrl[core]
                                       [rp2040_sio_interp_index(0, 1)] &
                         SIO_INTERP_CTRL_SIGNED) : false;

        for (base = 0; base < 2; base++) {
            uint16_t half = value >> (base * 16);
            bool sign = base01_signed ||
                        (s->interp_ctrl[core]
                                       [rp2040_sio_interp_index(interp,
                                                                base)] &
                         SIO_INTERP_CTRL_SIGNED);

            s->interp_base[core][rp2040_sio_interp_base_index(interp, base)] =
                sign ? (uint32_t)(int16_t)half : half;
        }
        break;
    default:
        break;
    }
}

static uint64_t rp2040_sio_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040SioState *s = opaque;
    unsigned core = rp2040_sio_current_core();
    unsigned interp;
    unsigned index;
    unsigned shift = (addr & 3) * 8;
    hwaddr interp_reg;
    hwaddr reg = addr & ~3ULL;
    uint64_t value;

    if (rp2040_sio_interp_offset(reg, &interp, &interp_reg)) {
        value = rp2040_sio_interp_read(s, core, interp, interp_reg);
        return extract64(value, shift, size * 8);
    }

    switch (reg) {
    case SIO_CPUID:
        value = core;
        break;
    case SIO_GPIO_IN:
        value = s->gpio_in;
        break;
    case SIO_GPIO_HI_IN:
        value = s->gpio_hi_in;
        break;
    case SIO_GPIO_OUT:
        value = s->gpio_out;
        break;
    case SIO_GPIO_OE:
        value = s->gpio_oe;
        break;
    case SIO_GPIO_HI_OUT:
        value = s->gpio_hi_out;
        break;
    case SIO_GPIO_HI_OE:
        value = s->gpio_hi_oe;
        break;
    case SIO_FIFO_ST:
        value = rp2040_sio_fifo_status(s, core);
        break;
    case SIO_FIFO_RD:
        value = rp2040_sio_fifo_pop(s, core);
        break;
    case SIO_SPINLOCK_ST:
        value = s->spinlock_st;
        break;
    case SIO_DIV_UDIVIDEND:
    case SIO_DIV_SDIVIDEND:
        value = s->div_dividend[core];
        break;
    case SIO_DIV_UDIVISOR:
    case SIO_DIV_SDIVISOR:
        value = s->div_divisor[core];
        break;
    case SIO_DIV_QUOTIENT:
        value = s->div_quotient[core];
        s->div_dirty[core] = false;
        break;
    case SIO_DIV_REMAINDER:
        value = s->div_remainder[core];
        break;
    case SIO_DIV_CSR:
        value = rp2040_sio_div_csr(s, core);
        break;
    default:
        if (rp2040_sio_spinlock_offset(reg, &index)) {
            value = (s->spinlock_st & BIT(index)) ? 0 : BIT(index);
            s->spinlock_st |= BIT(index);
        } else {
            value = 0;
            rp2040_log_unimplemented_read("sio", size,
                                          RP2040_SIO_BASE + addr, addr,
                                          value);
        }
        break;
    }

    return extract64(value, shift, size * 8);
}

static void rp2040_sio_write(void *opaque, hwaddr addr,
                             uint64_t value64, unsigned size)
{
    RP2040SioState *s = opaque;
    unsigned core = rp2040_sio_current_core();
    unsigned interp;
    unsigned index;
    unsigned shift = (addr & 3) * 8;
    hwaddr interp_reg;
    hwaddr reg = addr & ~3ULL;
    uint32_t value = (uint32_t)value64 << shift;

    if (rp2040_sio_interp_offset(reg, &interp, &interp_reg)) {
        rp2040_sio_interp_write(s, core, interp, interp_reg, value);
        return;
    }

    switch (reg) {
    case SIO_GPIO_OUT:
        s->gpio_out = value & SIO_GPIO_MASK;
        break;
    case SIO_GPIO_OUT_SET:
        s->gpio_out |= value & SIO_GPIO_MASK;
        break;
    case SIO_GPIO_OUT_CLR:
        s->gpio_out &= ~(value & SIO_GPIO_MASK);
        break;
    case SIO_GPIO_OUT_XOR:
        s->gpio_out ^= value & SIO_GPIO_MASK;
        break;
    case SIO_GPIO_OE:
        s->gpio_oe = value & SIO_GPIO_MASK;
        break;
    case SIO_GPIO_OE_SET:
        s->gpio_oe |= value & SIO_GPIO_MASK;
        break;
    case SIO_GPIO_OE_CLR:
        s->gpio_oe &= ~(value & SIO_GPIO_MASK);
        break;
    case SIO_GPIO_OE_XOR:
        s->gpio_oe ^= value & SIO_GPIO_MASK;
        break;
    case SIO_GPIO_HI_OUT:
        s->gpio_hi_out = value & SIO_GPIO_HI_MASK;
        break;
    case SIO_GPIO_HI_OUT_SET:
        s->gpio_hi_out |= value & SIO_GPIO_HI_MASK;
        break;
    case SIO_GPIO_HI_OUT_CLR:
        s->gpio_hi_out &= ~(value & SIO_GPIO_HI_MASK);
        break;
    case SIO_GPIO_HI_OUT_XOR:
        s->gpio_hi_out ^= value & SIO_GPIO_HI_MASK;
        break;
    case SIO_GPIO_HI_OE:
        s->gpio_hi_oe = value & SIO_GPIO_HI_MASK;
        break;
    case SIO_GPIO_HI_OE_SET:
        s->gpio_hi_oe |= value & SIO_GPIO_HI_MASK;
        break;
    case SIO_GPIO_HI_OE_CLR:
        s->gpio_hi_oe &= ~(value & SIO_GPIO_HI_MASK);
        break;
    case SIO_GPIO_HI_OE_XOR:
        s->gpio_hi_oe ^= value & SIO_GPIO_HI_MASK;
        break;
    case SIO_FIFO_ST:
        s->fifo_sticky[core] &= ~(value & SIO_FIFO_ST_WC_MASK);
        break;
    case SIO_FIFO_WR:
        rp2040_sio_fifo_push(s, core, value);
        break;
    case SIO_DIV_UDIVIDEND:
        s->div_dividend[core] = value;
        rp2040_sio_divide(s, core, false);
        break;
    case SIO_DIV_UDIVISOR:
        s->div_divisor[core] = value;
        rp2040_sio_divide(s, core, false);
        break;
    case SIO_DIV_SDIVIDEND:
        s->div_dividend[core] = value;
        rp2040_sio_divide(s, core, true);
        break;
    case SIO_DIV_SDIVISOR:
        s->div_divisor[core] = value;
        rp2040_sio_divide(s, core, true);
        break;
    case SIO_DIV_QUOTIENT:
        s->div_quotient[core] = value;
        s->div_dirty[core] = true;
        break;
    case SIO_DIV_REMAINDER:
        s->div_remainder[core] = value;
        s->div_dirty[core] = true;
        break;
    default:
        if (rp2040_sio_spinlock_offset(reg, &index)) {
            s->spinlock_st &= ~BIT(index);
        } else {
            rp2040_log_unimplemented_write("sio", size,
                                           RP2040_SIO_BASE + addr, addr,
                                           value64);
        }
        break;
    }
}

static const MemoryRegionOps rp2040_sio_ops = {
    .read = rp2040_sio_read,
    .write = rp2040_sio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void rp2040_sio_reset(DeviceState *dev)
{
    RP2040SioState *s = RP2040_SIO(dev);

    s->gpio_in &= SIO_GPIO_MASK;
    s->gpio_hi_in &= SIO_GPIO_HI_MASK;
    s->gpio_out = 0;
    s->gpio_oe = 0;
    s->gpio_hi_out = 0;
    s->gpio_hi_oe = 0;
    memset(s->fifo, 0, sizeof(s->fifo));
    memset(s->fifo_rptr, 0, sizeof(s->fifo_rptr));
    memset(s->fifo_wptr, 0, sizeof(s->fifo_wptr));
    memset(s->fifo_level, 0, sizeof(s->fifo_level));
    memset(s->fifo_sticky, 0, sizeof(s->fifo_sticky));
    memset(s->div_dividend, 0, sizeof(s->div_dividend));
    memset(s->div_divisor, 0, sizeof(s->div_divisor));
    memset(s->div_quotient, 0, sizeof(s->div_quotient));
    memset(s->div_remainder, 0, sizeof(s->div_remainder));
    memset(s->div_dirty, 0, sizeof(s->div_dirty));
    memset(s->interp_accum, 0, sizeof(s->interp_accum));
    memset(s->interp_base, 0, sizeof(s->interp_base));
    memset(s->interp_ctrl, 0, sizeof(s->interp_ctrl));
    s->spinlock_st = 0;
    rp2040_sio_update_fifo_irq(s);
}

static void rp2040_sio_init(Object *obj)
{
    RP2040SioState *s = RP2040_SIO(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_sio_ops, s,
                          TYPE_RP2040_SIO, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->fifo_irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->fifo_irq[1]);
}

static const VMStateDescription vmstate_rp2040_sio = {
    .name = TYPE_RP2040_SIO,
    .version_id = 3,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(gpio_out, RP2040SioState),
        VMSTATE_UINT32(gpio_in, RP2040SioState),
        VMSTATE_UINT32(gpio_hi_in, RP2040SioState),
        VMSTATE_UINT32(gpio_oe, RP2040SioState),
        VMSTATE_UINT32(gpio_hi_out, RP2040SioState),
        VMSTATE_UINT32(gpio_hi_oe, RP2040SioState),
        VMSTATE_UINT32_2DARRAY(fifo, RP2040SioState, RP2040_SIO_NUM_CORES,
                               RP2040_SIO_FIFO_DEPTH),
        VMSTATE_UINT8_ARRAY(fifo_rptr, RP2040SioState,
                            RP2040_SIO_NUM_CORES),
        VMSTATE_UINT8_ARRAY(fifo_wptr, RP2040SioState,
                            RP2040_SIO_NUM_CORES),
        VMSTATE_UINT8_ARRAY(fifo_level, RP2040SioState,
                            RP2040_SIO_NUM_CORES),
        VMSTATE_UINT32_ARRAY(fifo_sticky, RP2040SioState,
                             RP2040_SIO_NUM_CORES),
        VMSTATE_UINT32_ARRAY(div_dividend, RP2040SioState,
                             RP2040_SIO_NUM_CORES),
        VMSTATE_UINT32_ARRAY(div_divisor, RP2040SioState,
                             RP2040_SIO_NUM_CORES),
        VMSTATE_UINT32_ARRAY(div_quotient, RP2040SioState,
                             RP2040_SIO_NUM_CORES),
        VMSTATE_UINT32_ARRAY(div_remainder, RP2040SioState,
                             RP2040_SIO_NUM_CORES),
        VMSTATE_BOOL_ARRAY(div_dirty, RP2040SioState,
                           RP2040_SIO_NUM_CORES),
        VMSTATE_UINT32_2DARRAY_V(interp_accum, RP2040SioState,
                                 RP2040_SIO_NUM_CORES,
                                 RP2040_SIO_NUM_INTERPS *
                                 RP2040_SIO_INTERP_NUM_LANES, 3),
        VMSTATE_UINT32_2DARRAY_V(interp_base, RP2040SioState,
                                 RP2040_SIO_NUM_CORES,
                                 RP2040_SIO_NUM_INTERPS *
                                 RP2040_SIO_INTERP_NUM_BASES, 3),
        VMSTATE_UINT32_2DARRAY_V(interp_ctrl, RP2040SioState,
                                 RP2040_SIO_NUM_CORES,
                                 RP2040_SIO_NUM_INTERPS *
                                 RP2040_SIO_INTERP_NUM_LANES, 3),
        VMSTATE_UINT32(spinlock_st, RP2040SioState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property rp2040_sio_properties[] = {
    DEFINE_PROP_UINT32("gpio-in", RP2040SioState, gpio_in, 0),
    DEFINE_PROP_UINT32("gpio-hi-in", RP2040SioState, gpio_hi_in, 0),
};

static void rp2040_sio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_sio_reset);
    device_class_set_props(dc, rp2040_sio_properties);
    dc->vmsd = &vmstate_rp2040_sio;
}

static const TypeInfo rp2040_sio_info = {
    .name = TYPE_RP2040_SIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040SioState),
    .instance_init = rp2040_sio_init,
    .class_init = rp2040_sio_class_init,
};

static void rp2040_sio_register_types(void)
{
    type_register_static(&rp2040_sio_info);
}
type_init(rp2040_sio_register_types)
