/*
 *  ASPEED GPIO Controller
 *
 *  Copyright (C) 2017-2019 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/gpio/aspeed_gpio.h"
#include "include/hw/misc/aspeed_scu.h"
#include "qapi/error.h"
#include "qapi/visitor.h"

#define GPIOS_PER_REG 32
#define GPIOS_PER_SET GPIOS_PER_REG
#define GPIO_3_6V_REG_ARRAY_SIZE  (0x1f0 >> 2)
#define GPIO_PIN_GAP_SIZE 4
#define GPIOS_PER_GROUP 8
#define GPIO_GROUP_SHIFT 3

/* GPIO Source Types */
#define ASPEED_CMD_SRC_MASK         0x01010101
#define ASPEED_SOURCE_ARM           0
#define ASPEED_SOURCE_LPC           1
#define ASPEED_SOURCE_COPROCESSOR   2
#define ASPEED_SOURCE_RESERVED      3

/* GPIO Interrupt Triggers */
/*
 *  For each set of gpios there are three sensitivity registers that control
 *  the interrupt trigger mode.
 *
 *  | 2 | 1 | 0 | trigger mode
 *  -----------------------------
 *  | 0 | 0 | 0 | falling-edge
 *  | 0 | 0 | 1 | rising-edge
 *  | 0 | 1 | 0 | level-low
 *  | 0 | 1 | 1 | level-high
 *  | 1 | X | X | dual-edge
 */
#define ASPEED_FALLING_EDGE 0
#define ASPEED_RISING_EDGE  1
#define ASPEED_LEVEL_LOW    2
#define ASPEED_LEVEL_HIGH   3
#define ASPEED_DUAL_EDGE    4

/* GPIO Register Address Offsets */
#define GPIO_ABCD_DATA_VALUE       (0x000 >> 2)
#define GPIO_ABCD_DIRECTION        (0x004 >> 2)
#define GPIO_ABCD_INT_ENABLE       (0x008 >> 2)
#define GPIO_ABCD_INT_SENS_0       (0x00C >> 2)
#define GPIO_ABCD_INT_SENS_1       (0x010 >> 2)
#define GPIO_ABCD_INT_SENS_2       (0x014 >> 2)
#define GPIO_ABCD_INT_STATUS       (0x018 >> 2)
#define GPIO_ABCD_RESET_TOLERANT   (0x01C >> 2)
#define GPIO_EFGH_DATA_VALUE       (0x020 >> 2)
#define GPIO_EFGH_DIRECTION        (0x024 >> 2)
#define GPIO_EFGH_INT_ENABLE       (0x028 >> 2)
#define GPIO_EFGH_INT_SENS_0       (0x02C >> 2)
#define GPIO_EFGH_INT_SENS_1       (0x030 >> 2)
#define GPIO_EFGH_INT_SENS_2       (0x034 >> 2)
#define GPIO_EFGH_INT_STATUS       (0x038 >> 2)
#define GPIO_EFGH_RESET_TOLERANT   (0x03C >> 2)
#define GPIO_ABCD_DEBOUNCE_1       (0x040 >> 2)
#define GPIO_ABCD_DEBOUNCE_2       (0x044 >> 2)
#define GPIO_EFGH_DEBOUNCE_1       (0x048 >> 2)
#define GPIO_EFGH_DEBOUNCE_2       (0x04C >> 2)
#define GPIO_DEBOUNCE_TIME_1       (0x050 >> 2)
#define GPIO_DEBOUNCE_TIME_2       (0x054 >> 2)
#define GPIO_DEBOUNCE_TIME_3       (0x058 >> 2)
#define GPIO_ABCD_COMMAND_SRC_0    (0x060 >> 2)
#define GPIO_ABCD_COMMAND_SRC_1    (0x064 >> 2)
#define GPIO_EFGH_COMMAND_SRC_0    (0x068 >> 2)
#define GPIO_EFGH_COMMAND_SRC_1    (0x06C >> 2)
#define GPIO_IJKL_DATA_VALUE       (0x070 >> 2)
#define GPIO_IJKL_DIRECTION        (0x074 >> 2)
#define GPIO_MNOP_DATA_VALUE       (0x078 >> 2)
#define GPIO_MNOP_DIRECTION        (0x07C >> 2)
#define GPIO_QRST_DATA_VALUE       (0x080 >> 2)
#define GPIO_QRST_DIRECTION        (0x084 >> 2)
#define GPIO_UVWX_DATA_VALUE       (0x088 >> 2)
#define GPIO_UWVX_DIRECTION        (0x08C >> 2)
#define GPIO_IJKL_COMMAND_SRC_0    (0x090 >> 2)
#define GPIO_IJKL_COMMAND_SRC_1    (0x094 >> 2)
#define GPIO_IJKL_INT_ENABLE       (0x098 >> 2)
#define GPIO_IJKL_INT_SENS_0       (0x09C >> 2)
#define GPIO_IJKL_INT_SENS_1       (0x0A0 >> 2)
#define GPIO_IJKL_INT_SENS_2       (0x0A4 >> 2)
#define GPIO_IJKL_INT_STATUS       (0x0A8 >> 2)
#define GPIO_IJKL_RESET_TOLERANT   (0x0AC >> 2)
#define GPIO_IJKL_DEBOUNCE_1       (0x0B0 >> 2)
#define GPIO_IJKL_DEBOUNCE_2       (0x0B4 >> 2)
#define GPIO_IJKL_INPUT_MASK       (0x0B8 >> 2)
#define GPIO_ABCD_DATA_READ        (0x0C0 >> 2)
#define GPIO_EFGH_DATA_READ        (0x0C4 >> 2)
#define GPIO_IJKL_DATA_READ        (0x0C8 >> 2)
#define GPIO_MNOP_DATA_READ        (0x0CC >> 2)
#define GPIO_QRST_DATA_READ        (0x0D0 >> 2)
#define GPIO_UVWX_DATA_READ        (0x0D4 >> 2)
#define GPIO_YZAAAB_DATA_READ      (0x0D8 >> 2)
#define GPIO_AC_DATA_READ          (0x0DC >> 2)
#define GPIO_MNOP_COMMAND_SRC_0    (0x0E0 >> 2)
#define GPIO_MNOP_COMMAND_SRC_1    (0x0E4 >> 2)
#define GPIO_MNOP_INT_ENABLE       (0x0E8 >> 2)
#define GPIO_MNOP_INT_SENS_0       (0x0EC >> 2)
#define GPIO_MNOP_INT_SENS_1       (0x0F0 >> 2)
#define GPIO_MNOP_INT_SENS_2       (0x0F4 >> 2)
#define GPIO_MNOP_INT_STATUS       (0x0F8 >> 2)
#define GPIO_MNOP_RESET_TOLERANT   (0x0FC >> 2)
#define GPIO_MNOP_DEBOUNCE_1       (0x100 >> 2)
#define GPIO_MNOP_DEBOUNCE_2       (0x104 >> 2)
#define GPIO_MNOP_INPUT_MASK       (0x108 >> 2)
#define GPIO_QRST_COMMAND_SRC_0    (0x110 >> 2)
#define GPIO_QRST_COMMAND_SRC_1    (0x114 >> 2)
#define GPIO_QRST_INT_ENABLE       (0x118 >> 2)
#define GPIO_QRST_INT_SENS_0       (0x11C >> 2)
#define GPIO_QRST_INT_SENS_1       (0x120 >> 2)
#define GPIO_QRST_INT_SENS_2       (0x124 >> 2)
#define GPIO_QRST_INT_STATUS       (0x128 >> 2)
#define GPIO_QRST_RESET_TOLERANT   (0x12C >> 2)
#define GPIO_QRST_DEBOUNCE_1       (0x130 >> 2)
#define GPIO_QRST_DEBOUNCE_2       (0x134 >> 2)
#define GPIO_QRST_INPUT_MASK       (0x138 >> 2)
#define GPIO_UVWX_COMMAND_SRC_0    (0x140 >> 2)
#define GPIO_UVWX_COMMAND_SRC_1    (0x144 >> 2)
#define GPIO_UVWX_INT_ENABLE       (0x148 >> 2)
#define GPIO_UVWX_INT_SENS_0       (0x14C >> 2)
#define GPIO_UVWX_INT_SENS_1       (0x150 >> 2)
#define GPIO_UVWX_INT_SENS_2       (0x154 >> 2)
#define GPIO_UVWX_INT_STATUS       (0x158 >> 2)
#define GPIO_UVWX_RESET_TOLERANT   (0x15C >> 2)
#define GPIO_UVWX_DEBOUNCE_1       (0x160 >> 2)
#define GPIO_UVWX_DEBOUNCE_2       (0x164 >> 2)
#define GPIO_UVWX_INPUT_MASK       (0x168 >> 2)
#define GPIO_YZAAAB_COMMAND_SRC_0  (0x170 >> 2)
#define GPIO_YZAAAB_COMMAND_SRC_1  (0x174 >> 2)
#define GPIO_YZAAAB_INT_ENABLE     (0x178 >> 2)
#define GPIO_YZAAAB_INT_SENS_0     (0x17C >> 2)
#define GPIO_YZAAAB_INT_SENS_1     (0x180 >> 2)
#define GPIO_YZAAAB_INT_SENS_2     (0x184 >> 2)
#define GPIO_YZAAAB_INT_STATUS     (0x188 >> 2)
#define GPIO_YZAAAB_RESET_TOLERANT (0x18C >> 2)
#define GPIO_YZAAAB_DEBOUNCE_1     (0x190 >> 2)
#define GPIO_YZAAAB_DEBOUNCE_2     (0x194 >> 2)
#define GPIO_YZAAAB_INPUT_MASK     (0x198 >> 2)
#define GPIO_AC_COMMAND_SRC_0      (0x1A0 >> 2)
#define GPIO_AC_COMMAND_SRC_1      (0x1A4 >> 2)
#define GPIO_AC_INT_ENABLE         (0x1A8 >> 2)
#define GPIO_AC_INT_SENS_0         (0x1AC >> 2)
#define GPIO_AC_INT_SENS_1         (0x1B0 >> 2)
#define GPIO_AC_INT_SENS_2         (0x1B4 >> 2)
#define GPIO_AC_INT_STATUS         (0x1B8 >> 2)
#define GPIO_AC_RESET_TOLERANT     (0x1BC >> 2)
#define GPIO_AC_DEBOUNCE_1         (0x1C0 >> 2)
#define GPIO_AC_DEBOUNCE_2         (0x1C4 >> 2)
#define GPIO_AC_INPUT_MASK         (0x1C8 >> 2)
#define GPIO_ABCD_INPUT_MASK       (0x1D0 >> 2)
#define GPIO_EFGH_INPUT_MASK       (0x1D4 >> 2)
#define GPIO_YZAAAB_DATA_VALUE     (0x1E0 >> 2)
#define GPIO_YZAAAB_DIRECTION      (0x1E4 >> 2)
#define GPIO_AC_DATA_VALUE         (0x1E8 >> 2)
#define GPIO_AC_DIRECTION          (0x1EC >> 2)

static int aspeed_evaluate_irq(GPIOSets *regs, int gpio_prev_high, int gpio)
{
    uint32_t falling_edge = 0, rising_edge = 0;
    uint32_t int_trigger = extract32(regs->int_sens_0, gpio, 1)
                           | extract32(regs->int_sens_1, gpio, 1) << 1
                           | extract32(regs->int_sens_2, gpio, 1) << 2;
    uint32_t gpio_curr_high = extract32(regs->data_value, gpio, 1);
    uint32_t gpio_int_enabled = extract32(regs->int_enable, gpio, 1);

    if (!gpio_int_enabled) {
        return 0;
    }

    /* Detect edges */
    if (gpio_curr_high && !gpio_prev_high) {
        rising_edge = 1;
    } else if (!gpio_curr_high && gpio_prev_high) {
        falling_edge = 1;
    }

    if (((int_trigger == ASPEED_FALLING_EDGE)  && falling_edge)  ||
        ((int_trigger == ASPEED_RISING_EDGE)  && rising_edge)    ||
        ((int_trigger == ASPEED_LEVEL_LOW)  && !gpio_curr_high)  ||
        ((int_trigger == ASPEED_LEVEL_HIGH)  && gpio_curr_high)  ||
        ((int_trigger >= ASPEED_DUAL_EDGE)  && (rising_edge || falling_edge)))
    {
        regs->int_status = deposit32(regs->int_status, gpio, 1, 1);
        return 1;
    }
    return 0;
}

static void aspeed_gpio_update(AspeedGPIOState *s, GPIOSets *regs)
{
    uint32_t input_mask = regs->input_mask;
    uint32_t direction = regs->direction;
    uint32_t old = regs->data_value;
    uint32_t new = regs->data_read;
    uint32_t diff;
    int gpio;

    diff = old ^ new;
    if (!diff) {
        return;
    }

    if (!direction) {
        return;
    }

    for (gpio = 0; gpio < GPIOS_PER_REG; gpio++) {
        uint32_t mask = 1 << gpio;
        /* If the gpio needs to be updated... */
        if (!(diff & mask)) {
            continue;
        }
        /* ...and gpio pin is set to output...*/
        if (!(direction & mask)) {
            continue;
        }
        /* ...and isn't masked...  */
        if (input_mask & mask) {
            continue;
        }
        /* ... then update it*/
        if (mask & new) {
            regs->data_value |= mask;
        } else {
            regs->data_value &= ~mask;
        }

        if (aspeed_evaluate_irq(regs, old & mask, gpio)) {
            qemu_set_irq(s->irq[gpio], 1);
        }
    }
}

static uint32_t aspeed_adjust_pin(AspeedGPIOState *s, uint32_t pin)
{
    /*
     * The 2500 has a 4 pin gap in group AB and the 2400 has a 4 pin
     * gap in group Y (and only four pins in AB but this is the last group so
     * it doesn't matter).
     */
    if (s->ctrl->gap && pin >= s->ctrl->gap) {
        pin += GPIO_PIN_GAP_SIZE;
    }

    return pin;
}

static uint32_t aspeed_get_set_idx_from_pin(AspeedGPIOState *s, uint32_t pin)
{
    return aspeed_adjust_pin(s, pin) >> 5;
}

static bool aspeed_gpio_get_pin_level(AspeedGPIOState *s, uint32_t set_idx,
                                      uint32_t pin_mask)
{
    uint32_t reg_val;

    reg_val = s->sets[set_idx].data_value;

    return !!(reg_val & pin_mask);
}

static void aspeed_gpio_set_pin_level(AspeedGPIOState *s, uint32_t set_idx,
                                      uint32_t pin_mask, bool level)
{
    if (level) {
        s->sets[set_idx].data_read |= pin_mask;
    } else {
        s->sets[set_idx].data_read &= !pin_mask;
    }

    aspeed_gpio_update(s, &s->sets[set_idx]);
}

/*
 *  | src_1 | src_2 |  source     |
 *  |-----------------------------|
 *  |   0   |   0   |  ARM        |
 *  |   0   |   1   |  LPC        |
 *  |   1   |   0   |  Coprocessor|
 *  |   1   |   1   |  Reserved   |
 *
 *  Once the source of a set is programmed, corresponding bits in the
 *  data_value, direction, interrupt [enable, sens[0-2]], reset_tol and
 *  debounce registers can only be written by the source.
 *
 *  Source is ARM by default
 *  only bits 24, 16, 8, and 0 can be set
 *
 *  we don't currently have a model for the LPC or Coprocessor
 */
static uint32_t update_value_control_source(GPIOSets *regs, uint32_t old_value,
                                            uint32_t value)
{
    int i;
    int cmd_source;

    /* assume the source is always ARM for now */
    int source = ASPEED_SOURCE_ARM;

    uint32_t new_value = 0;

    /* for each group in set */
    for (i = 0; i < GPIOS_PER_REG; i += GPIOS_PER_GROUP) {
        cmd_source = extract32(regs->cmd_source_0, i, 1)
                | (extract32(regs->cmd_source_1, i, 1) << 1);

        if (source == cmd_source) {
            new_value |= (0xff << i) & value;
        } else {
            new_value |= (0xff << i) & old_value;
        }
    }
    return new_value;
}

/************* Reader helper functions ******************/
static uint32_t read_direction(GPIOSets *regs)
{
    return regs->direction;
}

static uint32_t read_data_value(GPIOSets *regs)
{
    return regs->data_value;
}

static uint32_t read_int_enable(GPIOSets *regs)
{
    return regs->int_enable;
}

static uint32_t read_int_sens_0(GPIOSets *regs)
{
    return regs->int_sens_0;
}

static uint32_t read_int_sens_1(GPIOSets *regs)
{
    return regs->int_sens_1;
}

static uint32_t read_int_sens_2(GPIOSets *regs)
{
    return regs->int_sens_2;
}

static uint32_t read_int_status(GPIOSets *regs)
{
    return regs->int_status;
}

static uint32_t read_reset_tol(GPIOSets *regs)
{
    return regs->reset_tol;
}

static uint32_t read_debounce_1(GPIOSets *regs)
{
    return regs->debounce_1;
}

static uint32_t read_debounce_2(GPIOSets *regs)
{
    return regs->debounce_2;
}

static uint32_t read_cmd_source_0(GPIOSets *regs)
{
    return regs->cmd_source_0;
}

static uint32_t read_cmd_source_1(GPIOSets *regs)
{
    return regs->cmd_source_1;
}

static uint32_t read_data(GPIOSets *regs)
{
    return regs->data_read;
}

static uint32_t read_input_mask(GPIOSets *regs)
{
    return regs->input_mask;
}

/************  Write helper functions *******************/
static void write_data_value(AspeedGPIOState *s, GPIOSets *regs,
                              const GPIOSetProperties *props, uint32_t val)
{
/*
 * If a pin is input only or doesn't exist then don't propagate the value.
 */
    val &= props->output;
    regs->data_read = update_value_control_source(regs, regs->data_read, val);
    aspeed_gpio_update(s, regs);
}

static void write_direction(AspeedGPIOState *s, GPIOSets *regs,
                            const GPIOSetProperties *props, uint32_t val)
{
/*
 *   where val is the value attempted to be written to the pin:
 *    pin type      | input mask | output mask | expected value
 *    ------------------------------------------------------------
 *   bidirectional  |   1       |   1        |  val
 *   input only     |   1       |   0        |   0
 *   output only    |   0       |   1        |   1
 *   no pin / gap   |   0       |   0        |   0
 *
 *  which is captured by:
 *  val = ( val | ~input) & output;
 */
    val = (val | ~props->input) & props->output;
    regs->direction = update_value_control_source(regs, regs->direction, val);
    aspeed_gpio_update(s, regs);
}

static void write_int_enable(AspeedGPIOState *s, GPIOSets *regs,
                             const GPIOSetProperties *props, uint32_t val)
{
    regs->int_enable = update_value_control_source(regs, regs->int_enable, val);
    aspeed_gpio_update(s, regs);
}

static void write_int_sens_0(AspeedGPIOState *s, GPIOSets *regs,
                             const GPIOSetProperties *props, uint32_t val)
{
    regs->int_sens_0 = update_value_control_source(regs, regs->int_sens_0, val);
    aspeed_gpio_update(s, regs);
}

static void write_int_sens_1(AspeedGPIOState *s, GPIOSets *regs,
                             const GPIOSetProperties *props, uint32_t val)
{
    regs->int_sens_1 = update_value_control_source(regs, regs->int_sens_1, val);
    aspeed_gpio_update(s, regs);
}

static void write_int_sens_2(AspeedGPIOState *s, GPIOSets *regs,
                             const GPIOSetProperties *props, uint32_t val)
{
    regs->int_sens_2 = update_value_control_source(regs, regs->int_sens_2, val);
    aspeed_gpio_update(s, regs);
}

static void write_int_status(AspeedGPIOState *s, GPIOSets *regs,
                             const GPIOSetProperties *props, uint32_t val)
{
    regs->int_status = val;
    aspeed_gpio_update(s, regs);
}

static void write_reset_tol(AspeedGPIOState *s, GPIOSets *regs,
                            const GPIOSetProperties *props, uint32_t val)
{
    regs->reset_tol = update_value_control_source(regs, regs->reset_tol, val);
}

static void write_debounce_1(AspeedGPIOState *s, GPIOSets *regs,
                             const GPIOSetProperties *props, uint32_t val)
{
    regs->debounce_1 = update_value_control_source(regs, regs->debounce_1, val);
}

static void write_debounce_2(AspeedGPIOState *s, GPIOSets *regs,
                             const GPIOSetProperties *props, uint32_t val)
{
    regs->debounce_2 = update_value_control_source(regs, regs->debounce_2, val);
}

static void write_cmd_source_0(AspeedGPIOState *s, GPIOSets *regs,
                               const GPIOSetProperties *props, uint32_t val)
{
    regs->cmd_source_0 = val & ASPEED_CMD_SRC_MASK;
}

static void write_cmd_source_1(AspeedGPIOState *s, GPIOSets *regs,
                               const GPIOSetProperties *props, uint32_t val)
{
    regs->cmd_source_1 = val & ASPEED_CMD_SRC_MASK;
}

/*
 * feeds into interrupt generation
 * 0: read from data value reg will be updated
 * 1: read from data value reg will not be updated
 */
static void write_input_mask(AspeedGPIOState *s, GPIOSets *regs,
                             const GPIOSetProperties *props, uint32_t val)
{
    regs->input_mask = val & props->input;
    aspeed_gpio_update(s, regs);
}

static const AspeedGPIOReg aspeed_3_6v_gpios[GPIO_3_6V_REG_ARRAY_SIZE] = {
    /* Set ABCD */
    [GPIO_ABCD_DATA_VALUE] =     {0, read_data_value, write_data_value},
    [GPIO_ABCD_DIRECTION] =      {0, read_direction, write_direction},
    [GPIO_ABCD_INT_ENABLE] =     {0, read_int_enable, write_int_enable},
    [GPIO_ABCD_INT_SENS_0] =     {0, read_int_sens_0, write_int_sens_0},
    [GPIO_ABCD_INT_SENS_1] =     {0, read_int_sens_1, write_int_sens_1},
    [GPIO_ABCD_INT_SENS_2] =     {0, read_int_sens_2, write_int_sens_2},
    [GPIO_ABCD_INT_STATUS] =     {0, read_int_status, write_int_status},
    [GPIO_ABCD_RESET_TOLERANT] = {0, read_reset_tol, write_reset_tol},
    [GPIO_ABCD_DEBOUNCE_1] =     {0, read_debounce_1, write_debounce_1},
    [GPIO_ABCD_DEBOUNCE_2] =     {0, read_debounce_2, write_debounce_2},
    [GPIO_ABCD_COMMAND_SRC_0] =  {0, read_cmd_source_0, write_cmd_source_0},
    [GPIO_ABCD_COMMAND_SRC_1] =  {0, read_cmd_source_1, write_cmd_source_1},
    [GPIO_ABCD_DATA_READ] =      {0, read_data, NULL},
    [GPIO_ABCD_INPUT_MASK] =     {0, read_input_mask, write_input_mask},
    /* Set EFGH */
    [GPIO_EFGH_DATA_VALUE] =     {1, read_data_value, write_data_value},
    [GPIO_EFGH_DIRECTION] =      {1, read_direction, write_direction },
    [GPIO_EFGH_INT_ENABLE] =     {1, read_int_enable, write_int_enable},
    [GPIO_EFGH_INT_SENS_0] =     {1, read_int_sens_0, write_int_sens_0},
    [GPIO_EFGH_INT_SENS_1] =     {1, read_int_sens_1, write_int_sens_1},
    [GPIO_EFGH_INT_SENS_2] =     {1, read_int_sens_2, write_int_sens_2},
    [GPIO_EFGH_INT_STATUS] =     {1, read_int_status, write_int_status},
    [GPIO_EFGH_RESET_TOLERANT] = {1, read_reset_tol,   write_reset_tol},
    [GPIO_EFGH_DEBOUNCE_1] =     {1, read_debounce_1,  write_debounce_1},
    [GPIO_EFGH_DEBOUNCE_2] =     {1, read_debounce_2,  write_debounce_2},
    [GPIO_EFGH_COMMAND_SRC_0] =  {1, read_cmd_source_0,  write_cmd_source_0},
    [GPIO_EFGH_COMMAND_SRC_1] =  {1, read_cmd_source_1,  write_cmd_source_1},
    [GPIO_EFGH_DATA_READ] =      {1, read_data, NULL},
    [GPIO_EFGH_INPUT_MASK] =     {1, read_input_mask,  write_input_mask},
    /* Set IJKL */
    [GPIO_IJKL_DATA_VALUE] =     {2, read_data_value, write_data_value},
    [GPIO_IJKL_DIRECTION] =      {2, read_direction, write_direction},
    [GPIO_IJKL_INT_ENABLE] =     {2, read_int_enable, write_int_enable},
    [GPIO_IJKL_INT_SENS_0] =     {2, read_int_sens_0, write_int_sens_0},
    [GPIO_IJKL_INT_SENS_1] =     {2, read_int_sens_1, write_int_sens_1},
    [GPIO_IJKL_INT_SENS_2] =     {2, read_int_sens_2, write_int_sens_2},
    [GPIO_IJKL_INT_STATUS] =     {2, read_int_status, write_int_status},
    [GPIO_IJKL_RESET_TOLERANT] = {2, read_reset_tol, write_reset_tol},
    [GPIO_IJKL_DEBOUNCE_1] =     {2, read_debounce_1, write_debounce_1},
    [GPIO_IJKL_DEBOUNCE_2] =     {2, read_debounce_2, write_debounce_2},
    [GPIO_IJKL_COMMAND_SRC_0] =  {2, read_cmd_source_0, write_cmd_source_0},
    [GPIO_IJKL_COMMAND_SRC_1] =  {2, read_cmd_source_1, write_cmd_source_1},
    [GPIO_IJKL_DATA_READ] =      {2, read_data, NULL},
    [GPIO_IJKL_INPUT_MASK] =     {2, read_input_mask, write_input_mask},
    /* Set MNOP */
    [GPIO_MNOP_DATA_VALUE] =     {3, read_data_value, write_data_value},
    [GPIO_MNOP_DIRECTION] =      {3, read_direction, write_direction},
    [GPIO_MNOP_INT_ENABLE] =     {3, read_int_enable, write_int_enable},
    [GPIO_MNOP_INT_SENS_0] =     {3, read_int_sens_0, write_int_sens_0},
    [GPIO_MNOP_INT_SENS_1] =     {3, read_int_sens_1, write_int_sens_1},
    [GPIO_MNOP_INT_SENS_2] =     {3, read_int_sens_2, write_int_sens_2},
    [GPIO_MNOP_INT_STATUS] =     {3, read_int_status, write_int_status},
    [GPIO_MNOP_RESET_TOLERANT] = {3, read_reset_tol,  write_reset_tol},
    [GPIO_MNOP_DEBOUNCE_1] =     {3, read_debounce_1, write_debounce_1},
    [GPIO_MNOP_DEBOUNCE_2] =     {3, read_debounce_2, write_debounce_2},
    [GPIO_MNOP_COMMAND_SRC_0] =  {3, read_cmd_source_0, write_cmd_source_0},
    [GPIO_MNOP_COMMAND_SRC_1] =  {3, read_cmd_source_1, write_cmd_source_1},
    [GPIO_MNOP_DATA_READ] =      {3, read_data, NULL},
    [GPIO_MNOP_INPUT_MASK] =     {3, read_input_mask, write_input_mask},
    /* Set QRST */
    [GPIO_QRST_DATA_VALUE] =     {4, read_data_value, write_data_value},
    [GPIO_QRST_DIRECTION] =      {4, read_direction, write_direction},
    [GPIO_QRST_INT_ENABLE] =     {4, read_int_enable, write_int_enable},
    [GPIO_QRST_INT_SENS_0] =     {4, read_int_sens_0, write_int_sens_0},
    [GPIO_QRST_INT_SENS_1] =     {4, read_int_sens_1, write_int_sens_1},
    [GPIO_QRST_INT_SENS_2] =     {4, read_int_sens_2, write_int_sens_2},
    [GPIO_QRST_INT_STATUS] =     {4, read_int_status, write_int_status},
    [GPIO_QRST_RESET_TOLERANT] = {4, read_reset_tol, write_reset_tol},
    [GPIO_QRST_DEBOUNCE_1] =     {4, read_debounce_1, write_debounce_1},
    [GPIO_QRST_DEBOUNCE_2] =     {4, read_debounce_2, write_debounce_2},
    [GPIO_QRST_COMMAND_SRC_0] =  {4, read_cmd_source_0, write_cmd_source_0},
    [GPIO_QRST_COMMAND_SRC_1] =  {4, read_cmd_source_1, write_cmd_source_1},
    [GPIO_QRST_DATA_READ] =      {4, read_data, NULL},
    [GPIO_QRST_INPUT_MASK] =     {4, read_input_mask,  write_input_mask},
    /* Set UVWX */
    [GPIO_UVWX_DATA_VALUE] =     {5, read_data_value, write_data_value},
    [GPIO_UWVX_DIRECTION] =      {5, read_direction, write_direction},
    [GPIO_UVWX_INT_ENABLE] =     {5, read_int_enable, write_int_enable},
    [GPIO_UVWX_INT_SENS_0] =     {5, read_int_sens_0, write_int_sens_0},
    [GPIO_UVWX_INT_SENS_1] =     {5, read_int_sens_1, write_int_sens_1},
    [GPIO_UVWX_INT_SENS_2] =     {5, read_int_sens_2, write_int_sens_2},
    [GPIO_UVWX_INT_STATUS] =     {5, read_int_status, write_int_status},
    [GPIO_UVWX_RESET_TOLERANT] = {5, read_reset_tol, write_reset_tol},
    [GPIO_UVWX_DEBOUNCE_1] =     {5, read_debounce_1, write_debounce_1},
    [GPIO_UVWX_DEBOUNCE_2] =     {5, read_debounce_2, write_debounce_2},
    [GPIO_UVWX_COMMAND_SRC_0] =  {5, read_cmd_source_0, write_cmd_source_0},
    [GPIO_UVWX_COMMAND_SRC_1] =  {5, read_cmd_source_1, write_cmd_source_1},
    [GPIO_UVWX_DATA_READ] =      {5, read_data, NULL},
    [GPIO_UVWX_INPUT_MASK] =     {5, read_input_mask, write_input_mask},
    /* Set YZAAAB */
    [GPIO_YZAAAB_DATA_VALUE] =     {6, read_data_value, write_data_value},
    [GPIO_YZAAAB_DIRECTION] =      {6, read_direction, write_direction},
    [GPIO_YZAAAB_INT_ENABLE] =     {6, read_int_enable, write_int_enable},
    [GPIO_YZAAAB_INT_SENS_0] =     {6, read_int_sens_0, write_int_sens_0},
    [GPIO_YZAAAB_INT_SENS_1] =     {6, read_int_sens_1, write_int_sens_1},
    [GPIO_YZAAAB_INT_SENS_2] =     {6, read_int_sens_2, write_int_sens_2},
    [GPIO_YZAAAB_INT_STATUS] =     {6, read_int_status, write_int_status},
    [GPIO_YZAAAB_RESET_TOLERANT] = {6, read_reset_tol, write_reset_tol},
    [GPIO_YZAAAB_DEBOUNCE_1] =     {6, read_debounce_1, write_debounce_1},
    [GPIO_YZAAAB_DEBOUNCE_2] =     {6, read_debounce_2, write_debounce_2},
    [GPIO_YZAAAB_COMMAND_SRC_0] =  {6, read_cmd_source_0, write_cmd_source_0},
    [GPIO_YZAAAB_COMMAND_SRC_1] =  {6, read_cmd_source_1, write_cmd_source_1},
    [GPIO_YZAAAB_DATA_READ] =      {6, read_data, NULL},
    [GPIO_YZAAAB_INPUT_MASK] =     {6, read_input_mask, write_input_mask},
    /* Set AC */
    [GPIO_AC_DATA_VALUE] =         {7, read_data_value, write_data_value},
    [GPIO_AC_DIRECTION] =          {7, read_direction, write_direction},
    [GPIO_AC_INT_ENABLE] =         {7, read_int_enable, write_int_enable},
    [GPIO_AC_INT_SENS_0] =         {7, read_int_sens_0, write_int_sens_0},
    [GPIO_AC_INT_SENS_1] =         {7, read_int_sens_1, write_int_sens_1},
    [GPIO_AC_INT_SENS_2] =         {7, read_int_sens_2, write_int_sens_2},
    [GPIO_AC_INT_STATUS] =         {7, read_int_status, write_int_status},
    [GPIO_AC_RESET_TOLERANT] =     {7, read_reset_tol, write_reset_tol},
    [GPIO_AC_DEBOUNCE_1] =         {7, read_debounce_1, write_debounce_1},
    [GPIO_AC_DEBOUNCE_2] =         {7, read_debounce_2, write_debounce_2},
    [GPIO_AC_COMMAND_SRC_0] =      {7, read_cmd_source_0, write_cmd_source_0},
    [GPIO_AC_COMMAND_SRC_1] =      {7, read_cmd_source_1, write_cmd_source_1},
    [GPIO_AC_DATA_READ] =          {7, read_data, NULL},
    [GPIO_AC_INPUT_MASK] =         {7, read_input_mask, write_input_mask},
};

static uint64_t aspeed_gpio_read(void *opaque, hwaddr offset, uint32_t size)
{
    AspeedGPIOState *s = ASPEED_GPIO(opaque);
    uint64_t idx = -1;
    const struct AspeedGPIOReg *reg;
    struct GPIOSets *set;
    uint32_t val = 0;

    idx = offset >> 2;
    if (idx >= GPIO_DEBOUNCE_TIME_1 && idx <= GPIO_DEBOUNCE_TIME_3) {
        idx -= GPIO_DEBOUNCE_TIME_1;
        return (uint64_t) s->debounce_regs[idx];
    }

    reg = &s->lookup[idx];
    if ((!reg->read) || (reg->set_idx >= s->ctrl->nr_gpio_sets)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset %lx\n",
                      __func__, offset);
        return 0;
    }

    set = &s->sets[reg->set_idx];
    val = reg->read(set);
    return (uint64_t) val;
}

static void aspeed_gpio_write(void *opaque, hwaddr offset, uint64_t data,
                              uint32_t size)
{
    AspeedGPIOState *s = ASPEED_GPIO(opaque);
    const GPIOSetProperties *props;
    uint64_t idx = -1;
    const struct AspeedGPIOReg *reg;
    struct GPIOSets *set;
    uint32_t mask;

    idx = offset >> 2;
    if (idx >= GPIO_DEBOUNCE_TIME_1 && idx <= GPIO_DEBOUNCE_TIME_3) {
        idx -= GPIO_DEBOUNCE_TIME_1;
        s->debounce_regs[idx] = (uint32_t) data;
        return;
    }

    reg = &s->lookup[idx];
    if ((!reg->write) || (reg->set_idx >= s->ctrl->nr_gpio_sets)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no setter for offset %lx\n",
                      __func__, offset);
        return;
    }
    set = &s->sets[reg->set_idx];

    props = &s->ctrl->props[reg->set_idx];
    mask = props->input | props->output;
    reg->write(s, set, props, data & mask);
}

static int get_set_idx(AspeedGPIOState *s, char *group, int *group_idx)
{
    int set_idx, g_idx = *group_idx;

    for (set_idx = 0; set_idx < s->ctrl->nr_gpio_sets; set_idx++) {
        const GPIOSetProperties *set_props = &s->ctrl->props[set_idx];
        for (g_idx = 0; g_idx < ASPEED_GROUPS_PER_SET; g_idx++) {
            if (!strncmp(group, set_props->group_label[g_idx], strlen(group))) {
                *group_idx = g_idx;
                return set_idx;
            }
        }
    }
    return -1;
}

static void aspeed_gpio_get_pin(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    int pin = 0xfff;
    bool level = true;
    char group[3];
    AspeedGPIOState *s = ASPEED_GPIO(obj);
    int set_idx, group_idx = 0;

    if (sscanf(name, "gpio%2[A-Z]%1d", group, &pin) != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: error reading %s\n",
                      __func__, name);
        return;
    }
    set_idx = get_set_idx(s, group, &group_idx);
    if (set_idx == -1) {
        return;
    }
    pin =  (1 << pin) << group_idx * GPIOS_PER_GROUP;
    level = aspeed_gpio_get_pin_level(s, set_idx, pin);
    visit_type_bool(v, name, &level, errp);
}

static void aspeed_gpio_set_pin(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    Error *local_err = NULL;
    bool level;
    int pin = 0xfff;
    char group[3];
    AspeedGPIOState *s = ASPEED_GPIO(obj);
    int set_idx, group_idx = 0;

    visit_type_bool(v, name, &level, &local_err);
    if (sscanf(name, "gpio%2[A-Z]%1d", group, &pin) != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: error reading %s\n",
                      __func__, name);
        return;
    }
    set_idx = get_set_idx(s, group, &group_idx);
    if (set_idx == -1) {
        return;
    }
    pin =  (1 << pin) << group_idx * GPIOS_PER_GROUP;
    aspeed_gpio_set_pin_level(s, set_idx, pin, level);
}


/****************** Setup functions ******************/

static const GPIOSetProperties ast2400_set_props[] = {
    [0] = {0xffffffff,  0xffffffff,  {"A", "B", "C", "D"} },
    [1] = {0xffffffff,  0xffffffff,  {"E", "F", "G", "H"} },
    [2] = {0xffffffff,  0xffffffff,  {"I", "J", "K", "L"} },
    [3] = {0xffffffff,  0xffffffff,  {"M", "N", "O", "P"} },
    [4] = {0xffffffff,  0xffffffff,  {"Q", "R", "S", "T"} },
    [5] = {0xffffffff,  0x0000ffff,  {"U", "V", "W", "X"} },
    [6] = {0x0000000f,  0x0fffff0f,  {"Y", "Z", "AA", "AB"} },
};

static const GPIOSetProperties ast2500_set_props[] = {
    [0] = {0xffffffff,  0xffffffff,  {"A", "B", "C", "D"} },
    [1] = {0xffffffff,  0xffffffff,  {"E", "F", "G", "H"} },
    [2] = {0xffffffff,  0xffffffff,  {"I", "J", "K", "L"} },
    [3] = {0xffffffff,  0xffffffff,  {"M", "N", "O", "P"} },
    [4] = {0xffffffff,  0xffffffff,  {"Q", "R", "S", "T"} },
    [5] = {0xffffffff,  0x0000ffff,  {"U", "V", "W", "X"} },
    [6] = {0xffffff0f,  0x0fffff0f,  {"Y", "Z", "AA", "AB"} },
    [7] = {0x000000ff,  0x000000ff,  {"AC"} },
};

static const AspeedGPIOController aspeed_gpio_ast2400_controller = {
    .props          = ast2400_set_props,
    .nr_gpio_pins   = 216,
    .nr_gpio_sets   = 7,
    .gap            = 196,
    .mem_size       = 0x19c,
};

static const AspeedGPIOController aspeed_gpio_ast2500_controller = {
    .props          = ast2500_set_props,
    .nr_gpio_pins   = 228,
    .nr_gpio_sets   = 8,
    .gap            = 220,
    .mem_size       = 0x1f0,
};
static const MemoryRegionOps aspeed_gpio_ops = {
    .read = aspeed_gpio_read,
    .write = aspeed_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aspeed_gpio_reset(DeviceState *dev)
{
    struct AspeedGPIOState *s = ASPEED_GPIO(dev);

    /* TODO: respect the reset tolerance registers */
    memset(s->sets, 0, sizeof(s->sets));
}

static void aspeed_gpio_realize(DeviceState *dev, Error **errp)
{
    AspeedGPIOState *s = ASPEED_GPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    int pin;

    for (pin = 0; pin < agc->ctrl->nr_gpio_pins; pin++) {
        sysbus_init_irq(sbd, &s->irq[pin]);
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_gpio_ops, s,
            TYPE_ASPEED_GPIO, agc->ctrl->mem_size);
    s->lookup = aspeed_3_6v_gpios;


    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_gpio_init(Object *obj)
{
    AspeedGPIOState *s = ASPEED_GPIO(obj);
    AspeedGPIOClass *agc = ASPEED_GPIO_GET_CLASS(s);
    int pin;

    s->ctrl = agc->ctrl;

    for (pin = 0; pin < agc->ctrl->nr_gpio_pins; pin++) {
        char *name;
        int set_idx = aspeed_get_set_idx_from_pin(s, pin);
        int pin_idx = aspeed_adjust_pin(s, pin) - (set_idx * GPIOS_PER_SET);
        int group_idx = pin_idx >> GPIO_GROUP_SHIFT;
        const GPIOSetProperties *props = &agc->ctrl->props[set_idx];

        name = g_strdup_printf("gpio%s%d", props->group_label[group_idx],
                               pin_idx % GPIOS_PER_GROUP);
        object_property_add(obj, name, "bool", aspeed_gpio_get_pin,
                            aspeed_gpio_set_pin, NULL, NULL, NULL);
    }
}

static const VMStateDescription vmstate_gpio_regs = {
    .name = TYPE_ASPEED_GPIO"/regs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(data_value,   GPIOSets),
        VMSTATE_UINT32(data_read,    GPIOSets),
        VMSTATE_UINT32(direction,    GPIOSets),
        VMSTATE_UINT32(int_enable,   GPIOSets),
        VMSTATE_UINT32(int_sens_0,   GPIOSets),
        VMSTATE_UINT32(int_sens_1,   GPIOSets),
        VMSTATE_UINT32(int_sens_2,   GPIOSets),
        VMSTATE_UINT32(int_status,   GPIOSets),
        VMSTATE_UINT32(reset_tol,    GPIOSets),
        VMSTATE_UINT32(cmd_source_0, GPIOSets),
        VMSTATE_UINT32(cmd_source_1, GPIOSets),
        VMSTATE_UINT32(debounce_1,   GPIOSets),
        VMSTATE_UINT32(debounce_2,   GPIOSets),
        VMSTATE_UINT32(input_mask,   GPIOSets),
        VMSTATE_END_OF_LIST(),
    }
};

static const VMStateDescription vmstate_aspeed_gpio = {
    .name = TYPE_ASPEED_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(sets, AspeedGPIOState, ASPEED_GPIO_MAX_NR_SETS,
                             1, vmstate_gpio_regs, GPIOSets),
        VMSTATE_UINT32_ARRAY(debounce_regs, AspeedGPIOState,
                             ASPEED_GPIO_NR_DEBOUNCE_REGS),
        VMSTATE_END_OF_LIST(),
   }
};

static void aspeed_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedGPIOClass *agc = ASPEED_GPIO_CLASS(klass);

    dc->realize = aspeed_gpio_realize;
    dc->reset = aspeed_gpio_reset;
    dc->desc = "Aspeed GPIO Controller";
    dc->vmsd = &vmstate_aspeed_gpio;
    agc->ctrl = data;
}

static const TypeInfo aspeed_gpio_info = {
    .name = TYPE_ASPEED_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedGPIOState),
    .class_size     = sizeof(AspeedGPIOClass),
    .abstract       = true,
};

static const TypeInfo aspeed_gpio_ast2400_info = {
    .name           = TYPE_ASPEED_GPIO "-ast2400",
    .parent = TYPE_ASPEED_GPIO,
    .class_init = aspeed_gpio_class_init,
    .instance_init = aspeed_gpio_init,
    .class_data = (void *)&aspeed_gpio_ast2400_controller,
};

static const TypeInfo aspeed_gpio_ast2500_info = {
    .name           = TYPE_ASPEED_GPIO "-ast2500",
    .parent = TYPE_ASPEED_GPIO,
    .class_init = aspeed_gpio_class_init,
    .instance_init = aspeed_gpio_init,
    .class_data = (void *)&aspeed_gpio_ast2500_controller,
};

static void aspeed_gpio_register_types(void)
{
    type_register_static(&aspeed_gpio_info);
    type_register_static(&aspeed_gpio_ast2400_info);
    type_register_static(&aspeed_gpio_ast2500_info);
}

type_init(aspeed_gpio_register_types);
