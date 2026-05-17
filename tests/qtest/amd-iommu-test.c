/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/i386/amd_iommu.h"

#define CMDBUF_ADDR       0x200000
#define CMDBUF_ENTRIES    256
#define CMDBUF_LEN_FIELD  8

static inline uint64_t amdvi_reg_readq(QTestState *s, uint64_t offset)
{
    return qtest_readq(s, AMDVI_BASE_ADDR + offset);
}

static inline void amdvi_reg_writeq(QTestState *s, uint64_t offset,
                                    uint64_t val)
{
    qtest_writeq(s, AMDVI_BASE_ADDR + offset, val);
}

static void test_cmdbuf_head_wrap(void)
{
    QTestState *s;
    uint64_t head;
    int i;
    /* 16 bytes per command */
    struct {
        uint64_t qw0;
        uint64_t qw1;
    } cmdbuf[CMDBUF_ENTRIES];

    s = qtest_init("-M q35 -device amd-iommu");

    /* write 256 COMPLETION_WAIT (no-op) commands to guest RAM */
    for (i = 0; i < CMDBUF_ENTRIES; i++) {
        cmdbuf[i].qw0 = (uint64_t)AMDVI_CMD_COMPLETION_WAIT << 60;
        cmdbuf[i].qw1 = 0;
    }
    qtest_memwrite(s, CMDBUF_ADDR, cmdbuf, sizeof(cmdbuf));

    /* point the IOMMU at the command buffer and set its length */
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_BASE,
                     CMDBUF_ADDR | ((uint64_t)CMDBUF_LEN_FIELD << 56));

    /* enable the IOMMU and its command buffer processor */
    amdvi_reg_writeq(s, AMDVI_MMIO_CONTROL,
                     AMDVI_MMIO_CONTROL_AMDVIEN | AMDVI_MMIO_CONTROL_CMDBUFLEN);

    /* advance tail to the last entry, consuming entries 0..254 */
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_TAIL,
                     (CMDBUF_ENTRIES - 1) * AMDVI_COMMAND_SIZE);

    /* wrap tail to 0, consuming entry 255 and completing the buffer */
    amdvi_reg_writeq(s, AMDVI_MMIO_COMMAND_TAIL, 0);

    /* after consuming all 256 entries the IOMMU must wrap CmdHeadPtr to 0 */
    head = amdvi_reg_readq(s, AMDVI_MMIO_COMMAND_HEAD);
    g_assert((head & AMDVI_MMIO_CMDBUF_HEAD_MASK) == 0);

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/q35/amd-iommu/cmdbuf-head-wrap", test_cmdbuf_head_wrap);
    return g_test_run();
}
