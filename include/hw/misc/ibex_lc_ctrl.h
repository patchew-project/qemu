/*
 * QEMU model of the Ibex Life Cycle Controller
 * SPEC Reference: https://docs.opentitan.org/hw/ip/lc_ctrl/doc/#register-table
 *
 * Copyright (C) 2022 Western Digital
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef IBEX_LC_CTRL_H
#define IBEX_LC_CTRL_H

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qom/object.h"
#include "hw/registerfields.h"
#include "qemu/timer.h"

#define TYPE_IBEX_LC_CTRL "ibex-lc"
#define IBEX_LC_CTRL(obj) \
    OBJECT_CHECK(IbexLCState, (obj), TYPE_IBEX_LC_CTRL)

/* LC Registers */
#define IBEX_LC_CTRL_ALERT_TEST                      (0x00 / 4) /* wo */
#define IBEX_CTRL_STATUS                             (0x04 / 4) /* ro */
#define IBEX_CTRL_CLAIM_TRANSITION_IF                (0x08 / 4) /* rw */
#define IBEX_LC_CTRL_TRANSITION_REGWEN               (0x0C / 4) /* ro */
#define IBEX_LC_CTRL_TRANSITION_CMD                  (0x10 / 4) /* r0w1c */
#define IBEX_LC_CTRL_TRANSITION_CTRL                 (0x14 / 4) /* rw1s*/
#define IBEX_LC_CTRL_TRANSITION_TOKEN_0              (0x18 / 4) /* rw */
#define IBEX_LC_CTRL_TRANSITION_TOKEN_1              (0x1C / 4) /* rw */
#define IBEX_LC_CTRL_TRANSITION_TOKEN_2              (0x20 / 4) /* rw */
#define IBEX_LC_CTRL_TRANSITION_TOKEN_3              (0x24 / 4) /* rw */
#define IBEX_LC_CTRL_TRANSITION_TARGET               (0x28 / 4) /* rw */
#define IBEX_LC_CTRL_OTP_VENDOR_TEST_CTRL            (0x2C / 4) /* rw */
#define IBEX_LC_CTRL_OTP_VENDOR_TEST_STATUS          (0x30 / 4) /* ro */
#define IBEX_LC_CTRL_LC_STATE                        (0x34 / 4) /* ro */
#define IBEX_LC_CTRL_LC_TRANSITION_CNT               (0x38 / 4) /* ro */
#define IBEX_LC_CTRL_LC_ID_STATE                     (0x3C / 4) /* ro */
#define IBEX_LC_CTRL_HW_REV                          (0x40 / 4) /* ro */
#define IBEX_LC_CTRL_DEVICE_ID_0                     (0x44 / 4) /* ro */
#define IBEX_LC_CTRL_DEVICE_ID_1                     (0x48 / 4) /* ro */
#define IBEX_LC_CTRL_DEVICE_ID_2                     (0x4C / 4) /* ro */
#define IBEX_LC_CTRL_DEVICE_ID_3                     (0x50 / 4) /* ro */
#define IBEX_LC_CTRL_DEVICE_ID_4                     (0x54 / 4) /* ro */
#define IBEX_LC_CTRL_DEVICE_ID_5                     (0x58 / 4) /* ro */
#define IBEX_LC_CTRL_DEVICE_ID_6                     (0x5C / 4) /* ro */
#define IBEX_LC_CTRL_DEVICE_ID_7                     (0x60 / 4) /* ro */
#define IBEX_LC_CTRL_MANUF_STATE_0                   (0x64 / 4) /* ro */
#define IBEX_LC_CTRL_MANUF_STATE_1                   (0x68 / 4) /* ro */
#define IBEX_LC_CTRL_MANUF_STATE_2                   (0x6C / 4) /* ro */
#define IBEX_LC_CTRL_MANUF_STATE_3                   (0x70 / 4) /* ro */
#define IBEX_LC_CTRL_MANUF_STATE_4                   (0x74 / 4) /* ro */
#define IBEX_LC_CTRL_MANUF_STATE_5                   (0x78 / 4) /* ro */
#define IBEX_LC_CTRL_MANUF_STATE_6                   (0x7C / 4) /* ro */
#define IBEX_LC_CTRL_MANUF_STATE_7                   (0x80 / 4) /* ro */

/*  Max Register (Based on addr) */
#define IBEX_LC_NUM_REGS           (IBEX_LC_CTRL_MANUF_STATE_7 + 1)

/*
 * Lifecycle States
 * These are magic values that set particular system state
 * See here: https://docs.opentitan.org/hw/ip/lc_ctrl/doc/#Reg_lc_state
 */
/* Unlocked test state where debug functions are enabled. */
#define LC_STATE_RAW                0x00000000
#define LC_STATE_TEST_UNLOCKED0     0x02108421
#define LC_STATE_TEST_LCOKED0       0x04210842
#define LC_STATE_TEST_UNLOCKED1     0x06318c63
#define LC_STATE_TEST_LOCKED1       0x08421084
#define LC_STATE_TEST_UNLOCKED2     0x0a5294a5
#define LC_STATE_TEST_LOCKED2       0x0c6318c6
#define LC_STATE_TEST_UNLOCKED3     0x0e739ce7
#define LC_STATE_TEST_LOCKED3       0x10842108
#define LC_STATE_TEST_UNLOCKED4     0x1294a529
#define LC_STATE_TEST_LOCKED4       0x14a5294a
#define LC_STATE_TEST_UNLOCKED5     0x16b5ad6b
#define LC_STATE_TEST_LOCKED5       0x18c6318c
#define LC_STATE_TEST_UNLOCKED6     0x1ad6b5ad
#define LC_STATE_TEST_LOCKED6       0x1ce739ce
#define LC_STATE_TEST_UNLOCKED7     0x1ef7bdef
#define LC_STATE_DEV                0x21084210
#define LC_STATE_PROD               0x2318c631
#define LC_STATE_PROD_END           0x25294a52
#define LC_STATE_RMA                0x2739ce73
#define LC_STATE_SCRAP              0x294a5294
#define LC_STATE_POST_TRANSITION    0x2b5ad6b5
#define LC_STATE_ESCALATE           0x2d6b5ad6
#define LC_STATE_INVALID            0x2f7bdef7


typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;
    uint32_t regs[IBEX_LC_NUM_REGS];

} IbexLCState;


#endif
