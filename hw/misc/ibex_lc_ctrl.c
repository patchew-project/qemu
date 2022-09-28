/*
 * QEMU model of the Ibex Life Cycle Controller
 * SPEC Reference: https://docs.opentitan.org/hw/ip/lc_ctrl/doc/
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/ibex_lc_ctrl.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "trace.h"

REG32(ALERT_TEST, 0x00)
    FIELD(ALERT_TEST, FETAL_PROG_ERR, 0, 1)
    FIELD(ALERT_TEST, FETAL_STATE_ERR, 1, 1)
    FIELD(ALERT_TEST, FETAL_BUS_INTEG_ERR, 2, 1)
REG32(CTRL_STATUS, 0x04)
    FIELD(CTRL_STATUS, READY, 0, 1)
    FIELD(CTRL_STATUS, TRANSITION_SUCCESSFUL, 0, 1)
    FIELD(CTRL_STATUS, TRANSITION_COUNT_ERROR, 1, 1)
    FIELD(CTRL_STATUS, TRANSITION_ERROR, 2, 1)
    FIELD(CTRL_STATUS, TOKEN_ERROR, 3, 1)
    FIELD(CTRL_STATUS, FLASH_RMA_ERROR, 4, 1)
    FIELD(CTRL_STATUS, OTP_ERROR, 5, 1)
    FIELD(CTRL_STATUS, STATE_ERROR, 6, 1)
    FIELD(CTRL_STATUS, BUS_INTEG_ERROR, 7, 1)
    FIELD(CTRL_STATUS, OTP_PARTITION_ERROR, 8, 1)
REG32(CLAIM_TRANSITION_IF, 0x08)
     FIELD(CLAIM_TRANSITION_IF, MUTEX, 0, 8)
REG32(TRANSITION_REGWEN , 0x0C)
     FIELD(TRANSITION_REGWEN , TRANSITION_REGWEN, 0, 1)
REG32(TRANSITION_CMD , 0x10)
     FIELD(TRANSITION_CMD , START, 0, 1)
REG32(TRANSITION_CTRL , 0x14)
     FIELD(TRANSITION_CTRL , EXT_CLOCK_EN, 0, 1)
REG32(TRANSITION_TOKEN_0 , 0x18)
     FIELD(TRANSITION_TOKEN_0 , TRANSITION_TOKEN_0, 0, 32)
REG32(TRANSITION_TOKEN_1 , 0x1C)
     FIELD(TRANSITION_TOKEN_1 , TRANSITION_TOKEN_1, 0, 32)
REG32(TRANSITION_TOKEN_2 , 0x20)
     FIELD(TRANSITION_TOKEN_2 , TRANSITION_TOKEN_2, 0, 32)
REG32(TRANSITION_TOKEN_3 , 0x24)
     FIELD(TRANSITION_TOKEN_3 , TRANSITION_TOKEN_3, 0, 32)
REG32(TRANSITION_TARGET , 0x28)
     FIELD(TRANSITION_TARGET , STATE, 0, 30)
REG32(OTP_VENDOR_TEST_CTRL , 0x2C)
     FIELD(OTP_VENDOR_TEST_CTRL , OTP_VENDOR_TEST_CTRL, 0, 32)
REG32(OTP_VENDOR_TEST_STATUS , 0x30)
     FIELD(OTP_VENDOR_TEST_STATUS , OTP_VENDOR_TEST_STATUS, 0, 32)
REG32(LC_STATE , 0x34)
     FIELD(LC_STATE , STATE, 0, 30)
REG32(LC_TRANSITION_CNT , 0x38)
     FIELD(LC_TRANSITION_CNT , CNT, 0, 5)
REG32(LC_ID_STATE , 0x3C)
     FIELD(LC_ID_STATE , STATE, 0, 32)
REG32(HW_REV , 0x40)
     FIELD(HW_REV , CHIP_REV, 0, 16)
     FIELD(HW_REV , CHIP_GEN, 16, 16)
REG32(DEVICE_ID_0 , 0x44)
     FIELD(DEVICE_ID_0 , DEVICE_ID_0, 0, 32)
REG32(DEVICE_ID_1 , 0x48)
     FIELD(DEVICE_ID_1 , DEVICE_ID_2, 0, 32)
REG32(DEVICE_ID_2 , 0x4C)
     FIELD(DEVICE_ID_2 , DEVICE_ID_2, 0, 32)
REG32(DEVICE_ID_3 , 0x50)
     FIELD(DEVICE_ID_3 , DEVICE_ID_3, 0, 32)
REG32(DEVICE_ID_4 , 0x54)
     FIELD(DEVICE_ID_4 , DEVICE_ID_4, 0, 32)
REG32(DEVICE_ID_5 , 0x58)
     FIELD(DEVICE_ID_5 , DEVICE_ID_5, 0, 32)
REG32(DEVICE_ID_6 , 0x5C)
     FIELD(DEVICE_ID_6 , DEVICE_ID_6, 0, 32)
REG32(DEVICE_ID_7 , 0x60)
     FIELD(DEVICE_ID_7 , DEVICE_ID_7, 0, 32)
REG32(MANUF_STATE_0 , 0x64)
     FIELD(MANUF_STATE_0 , MANUF_STATE_0, 0, 32)
REG32(MANUF_STATE_1 , 0x68)
     FIELD(MANUF_STATE_1 , MANUF_STATE_1, 0, 32)
REG32(MANUF_STATE_2 , 0x6C)
     FIELD(MANUF_STATE_2 , MANUF_STATE_2, 0, 32)
REG32(MANUF_STATE_3 , 0x70)
     FIELD(MANUF_STATE_3 , MANUF_STATE_3, 0, 32)
REG32(MANUF_STATE_4 , 0x74)
     FIELD(MANUF_STATE_4 , MANUF_STATE_4, 0, 32)
REG32(MANUF_STATE_5 , 0x78)
     FIELD(MANUF_STATE_5 , MANUF_STATE_5, 0, 32)
REG32(MANUF_STATE_6 , 0x7C)
     FIELD(MANUF_STATE_6 , MANUF_STATE_6, 0, 32)
REG32(MANUF_STATE_7 , 0x80)
     FIELD(MANUF_STATE_7 , MANUF_STATE_7, 0, 32)

static void ibex_lc_reset(DeviceState *dev)
{
    IbexLCState *s = IBEX_LC_CTRL(dev);

    trace_ibex_lc_general("Resetting Ibex Life-cycle IP");

    /* Set all register values to spec defaults */
    s->regs[IBEX_LC_CTRL_ALERT_TEST]                = 0x00;
    s->regs[IBEX_CTRL_STATUS]                       = 0x00;
    s->regs[IBEX_CTRL_CLAIM_TRANSITION_IF]          = 0x69;
    s->regs[IBEX_LC_CTRL_TRANSITION_REGWEN]         = 0x0C;
    s->regs[IBEX_LC_CTRL_TRANSITION_CMD]            = 0x10;
    s->regs[IBEX_LC_CTRL_TRANSITION_CTRL]           = 0x14;
    s->regs[IBEX_LC_CTRL_TRANSITION_TOKEN_0]        = 0x00;
    s->regs[IBEX_LC_CTRL_TRANSITION_TOKEN_1]        = 0x00;
    s->regs[IBEX_LC_CTRL_TRANSITION_TOKEN_2]        = 0x00;
    s->regs[IBEX_LC_CTRL_TRANSITION_TOKEN_3]        = 0x00;
    s->regs[IBEX_LC_CTRL_TRANSITION_TARGET]         = 0x00;
    s->regs[IBEX_LC_CTRL_OTP_VENDOR_TEST_CTRL]      = 0x00;
    s->regs[IBEX_LC_CTRL_OTP_VENDOR_TEST_STATUS]    = 0x00;
    /* This is checked by the boot rom to set state */
    s->regs[IBEX_LC_CTRL_LC_STATE]                  = LC_STATE_TEST_UNLOCKED0;
    s->regs[IBEX_LC_CTRL_LC_TRANSITION_CNT]         = 0x00;
    s->regs[IBEX_LC_CTRL_LC_ID_STATE]               = 0x00;
    s->regs[IBEX_LC_CTRL_HW_REV]                    = 0x00;
    s->regs[IBEX_LC_CTRL_DEVICE_ID_0]               = 0x00;
    s->regs[IBEX_LC_CTRL_DEVICE_ID_1]               = 0x00;
    s->regs[IBEX_LC_CTRL_DEVICE_ID_2]               = 0x00;
    s->regs[IBEX_LC_CTRL_DEVICE_ID_3]               = 0x00;
    s->regs[IBEX_LC_CTRL_DEVICE_ID_4]               = 0x00;
    s->regs[IBEX_LC_CTRL_DEVICE_ID_5]               = 0x00;
    s->regs[IBEX_LC_CTRL_DEVICE_ID_6]               = 0x00;
    s->regs[IBEX_LC_CTRL_DEVICE_ID_7]               = 0x00;
    s->regs[IBEX_LC_CTRL_MANUF_STATE_0]             = 0x00;
    s->regs[IBEX_LC_CTRL_MANUF_STATE_1]             = 0x00;
    s->regs[IBEX_LC_CTRL_MANUF_STATE_2]             = 0x00;
    s->regs[IBEX_LC_CTRL_MANUF_STATE_3]             = 0x00;
    s->regs[IBEX_LC_CTRL_MANUF_STATE_4]             = 0x00;
    s->regs[IBEX_LC_CTRL_MANUF_STATE_5]             = 0x00;
    s->regs[IBEX_LC_CTRL_MANUF_STATE_6]             = 0x00;
    s->regs[IBEX_LC_CTRL_MANUF_STATE_7]             = 0x00;
}

static uint64_t ibex_lc_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    IbexLCState *s = opaque;
    uint32_t reg_val = 0;

    trace_ibex_lc_read(addr, size);
    /* Match reg index */
    addr = addr >> 2;

    /* The only wo register is IBEX_LC_CTRL_ALERT_TEST, let's skip it */
    switch (addr) {
    case IBEX_CTRL_STATUS...IBEX_LC_CTRL_MANUF_STATE_7:
        reg_val = s->regs[addr];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Bad offset 0x%" HWADDR_PRIx "\n",
                        addr << 2);
    }
    return reg_val;
}

static void ibex_lc_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    IbexLCState *s = opaque;
    uint32_t val32 = val64;

    trace_ibex_lc_write(addr, size, val64);

    /* Match reg index */
    addr = addr >> 2;
    switch (addr) {
    /* Skipping any R/O registers */
    case IBEX_LC_CTRL_ALERT_TEST:
        s->regs[addr] = val32;
        qemu_log_mask(LOG_UNIMP,
                      "%s: LC_CTRL_ALERT_TEST not supported\n", __func__);
        break;
    case IBEX_CTRL_CLAIM_TRANSITION_IF:
        s->regs[addr] = val32;
        qemu_log_mask(LOG_UNIMP,
                      "%s: LC_CTRL_CLAIM_TRANSITION_IF not supported\n",
                      __func__);
        break;
    case IBEX_LC_CTRL_TRANSITION_CMD:
        s->regs[addr] = val32;
        qemu_log_mask(LOG_UNIMP,
                      "%s: LC_CTRL_TRANSITION_CMD not supported\n", __func__);
        break;
    case IBEX_LC_CTRL_TRANSITION_CTRL:
        s->regs[addr] = val32;
        qemu_log_mask(LOG_UNIMP,
                      "%s: LC_CTRL_TRANSITION_CTRL not supported\n", __func__);
        break;
    case IBEX_LC_CTRL_TRANSITION_TOKEN_0...IBEX_LC_CTRL_TRANSITION_TOKEN_3:
        s->regs[addr] = val32;
        qemu_log_mask(LOG_UNIMP,
                      "%s: LC_CTRL_TRANSITION_TOKENS not supported\n",
                      __func__);
        break;
    case IBEX_LC_CTRL_TRANSITION_TARGET:
        s->regs[addr] = val32;
        qemu_log_mask(LOG_UNIMP,
                      "%s: LC_CTRL_TRANSITION_TARGET not supported\n",
                      __func__);
        break;
    case IBEX_LC_CTRL_OTP_VENDOR_TEST_CTRL:
        s->regs[addr] = val32;
        qemu_log_mask(LOG_UNIMP,
                      "%s: LC_CTRL_VENDOR_TEST not supported\n",
                      __func__);
        break;
    default:
        /* The remaining registers are all ro, or bad offset */
        qemu_log_mask(LOG_GUEST_ERROR, "Bad offset 0x%" HWADDR_PRIx "\n",
                      addr << 2);
    }
}

static const MemoryRegionOps ibex_lc_ops = {
    .read = ibex_lc_read,
    .write = ibex_lc_write,
    /* Ibex default LE */
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_ibex = {
    .name = TYPE_IBEX_LC_CTRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IbexLCState, IBEX_LC_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void ibex_lc_init(Object *obj)
{
    IbexLCState *s = IBEX_LC_CTRL(obj);

    trace_ibex_lc_general("Ibex Life-cycle IP Init");

    memory_region_init_io(&s->mmio, obj, &ibex_lc_ops, s,
                          TYPE_IBEX_LC_CTRL, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void ibex_lc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = NULL;
    dc->reset = ibex_lc_reset;
    dc->vmsd = &vmstate_ibex;
}

static const TypeInfo ibex_lc_info = {
    .name          = TYPE_IBEX_LC_CTRL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbexLCState),
    .instance_init = ibex_lc_init,
    .class_init    = ibex_lc_class_init,
};

static void ibex_lc_register_types(void)
{
    type_register_static(&ibex_lc_info);
}

type_init(ibex_lc_register_types)
