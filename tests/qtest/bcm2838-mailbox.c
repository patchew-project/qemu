/*
 * Helper functions to work with BCM2838 mailbox via qtest interface.
 *
 * Copyright (c) 2023 Auriga LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "libqtest-single.h"
#include "bcm2838-mailbox.h"


static uint32_t qtest_mbox0_read_reg32(QTestState *s, uint32_t offset)
{
    return qtest_readl(s, MBOX0_BASE + offset);
}

static void qtest_mbox1_write_reg32(QTestState *s, uint32_t offset, uint32_t value)
{
    return qtest_writel(s, MBOX1_BASE + offset, value);
}

static void qtest_mbox1_write(QTestState *s, uint8_t channel, uint32_t data)
{
    uint32_t reg;

    reg = FIELD_DP32(reg, MBOX_WRITE_REG, CHANNEL, channel);
    reg = FIELD_DP32(reg, MBOX_WRITE_REG, DATA, data);
    qtest_mbox1_write_reg32(s, MBOX_REG_WRITE, reg);
}

int qtest_mbox0_has_data(QTestState *s) {
    return !(qtest_mbox0_read_reg32(s, MBOX_REG_STATUS) & MBOX_READ_EMPTY);
}

int mbox0_has_data(void) {
    return qtest_mbox0_has_data(global_qtest);
}

void qtest_mbox0_read_message(QTestState *s,
                              uint8_t channel,
                              void *msgbuf,
                              size_t msgbuf_size)
{
    uint32_t reg;
    uint32_t msgaddr;

    g_assert(qtest_mbox0_has_data(s));
    reg = qtest_mbox0_read_reg32(s, MBOX_REG_READ);
    g_assert_cmphex(FIELD_EX32(reg, MBOX_WRITE_REG, CHANNEL), ==, channel);
    msgaddr = FIELD_EX32(reg, MBOX_WRITE_REG, DATA) << 4;
    qtest_memread(s, msgaddr, msgbuf, msgbuf_size);
}

void mbox0_read_message(uint8_t channel, void *msgbuf, size_t msgbuf_size) {
    qtest_mbox0_read_message(global_qtest, channel, msgbuf, msgbuf_size);
}

void qtest_mbox1_write_message(QTestState *s, uint8_t channel, uint32_t msg_addr)
{
    qtest_mbox1_write(s, channel, msg_addr >> 4);
}


void mbox1_write_message(uint8_t channel, uint32_t msg_addr)
{
    qtest_mbox1_write_message(global_qtest, channel, msg_addr);
}
