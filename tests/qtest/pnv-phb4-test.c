/*
 * QTest testcase for PowerNV PHB4
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/pci-host/pnv_phb4_regs.h"

#define P10_XSCOM_BASE          0x000603fc00000000ull
#define PHB4_MMIO               0x000600c3c0000000ull
#define PHB4_XSCOM              0x8010900ull

#define PPC_BIT(bit)            (0x8000000000000000ULL >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))

static uint64_t pnv_xscom_addr(uint32_t pcba)
{
    return P10_XSCOM_BASE | ((uint64_t) pcba << 3);
}

static uint64_t pnv_phb4_xscom_addr(uint32_t reg)
{
    return pnv_xscom_addr(PHB4_XSCOM + reg);
}

/*
 * XSCOM read/write is indirect in PHB4:
 * Write 'SCOM - HV Indirect Address Register'
 * with register-offset to read/write.
   - bit[0]: Valid Bit
   - bit[51:61]: Indirect Address(00:10)
 * Read/write 'SCOM - HV Indirect Data Register' to get/set the value.
 */
static void pnv_phb4_xscom_write(QTestState *qts, uint32_t reg, uint64_t val)
{
    qtest_writeq(qts, pnv_phb4_xscom_addr(PHB_SCOM_HV_IND_ADDR),
            PPC_BIT(0) | reg);
    qtest_writeq(qts, pnv_phb4_xscom_addr(PHB_SCOM_HV_IND_DATA), val);
}
static uint64_t pnv_phb4_xscom_read(QTestState *qts, uint32_t reg)
{
    qtest_writeq(qts, pnv_phb4_xscom_addr(PHB_SCOM_HV_IND_ADDR),
            PPC_BIT(0) | reg);
    return qtest_readq(qts, pnv_phb4_xscom_addr(PHB_SCOM_HV_IND_DATA));
}

/* Assert that 'PHB - Version Register Offset 0x0800' bits-[24:31] are 0xA5 */
static void phb4_version_test(QTestState *qts)
{
    uint64_t ver = pnv_phb4_xscom_read(qts, PHB_VERSION);

    /* PHB Version register [24:31]: Major Revision ID 0xA5 */
    ver = ver >> (63 - 31);
    g_assert_cmpuint(ver, ==, 0xA5);
}

/* Assert that 'PHB PBL Control' register has correct reset value */
static void phb4_reset_test(QTestState *qts)
{
    g_assert_cmpuint(pnv_phb4_xscom_read(qts, PHB_PBL_CONTROL),
                     ==, 0xC009000000000000);
}

/* Check sticky-reset */
static void phb4_sticky_rst_test(QTestState *qts)
{
    uint64_t val;

    /*
     * Sticky reset test of PHB_PBL_ERR_STATUS.
     *
     * Write all 1's to reg PHB_PBL_ERR_INJECT.
     * RO-only bits will not be written and
     * updated value will be copied to reg PHB_PBL_ERR_STATUS.
     *
     * Reset PBL core by setting PHB_PCIE_CRESET_PBL in reg PHB_PCIE_CRESET.
     * Verify the sticky bits are still set.
     */
    pnv_phb4_xscom_write(qts, PHB_PBL_ERR_INJECT, PPC_BITMASK(0, 63));
    pnv_phb4_xscom_write(qts, PHB_PCIE_CRESET, PHB_PCIE_CRESET_PBL); /*Reset*/
    val = pnv_phb4_xscom_read(qts, PHB_PBL_ERR_STATUS);
    g_assert_cmpuint(val, ==, 0xF00DFD8E00);
}

/* Check that write-only bits/regs return 0 when read */
static void phb4_writeonly_read_test(QTestState *qts)
{
    uint64_t val;

    /*
     * Set all bits of PHB_DMA_SYNC,
     * bits 0 and 2 are write-only and should be read as 0.
     */
    pnv_phb4_xscom_write(qts, PHB_DMA_SYNC, PPC_BITMASK(0, 63));
    val = pnv_phb4_xscom_read(qts, PHB_DMA_SYNC);
    g_assert_cmpuint(val & PPC_BIT(0), ==, 0x0);
    g_assert_cmpuint(val & PPC_BIT(2), ==, 0x0);

    /*
     * Set all bits of PHB_PCIE_HOTPLUG_STATUS,
     * bit 9 is write-only and should be read as 0.
     */
    pnv_phb4_xscom_write(qts, PHB_PCIE_HOTPLUG_STATUS, PPC_BITMASK(0, 63));
    val = pnv_phb4_xscom_read(qts, PHB_PCIE_HOTPLUG_STATUS);
    g_assert_cmpuint(val & PPC_BIT(9), ==, 0x0);

    /*
     * Set all bits of PHB_PCIE_LMR,
     * bits 0 and 1 are write-only and should be read as 0.
     */
    pnv_phb4_xscom_write(qts, PHB_PCIE_LMR, PPC_BITMASK(0, 63));
    val = pnv_phb4_xscom_read(qts, PHB_PCIE_LMR);
    g_assert_cmpuint(val & PPC_BIT(0), ==, 0x0);
    g_assert_cmpuint(val & PPC_BIT(1), ==, 0x0);

    /*
     * Set all bits of PHB_PCIE_DLP_TRWCTL,
     * write-only bit-1 should be read as 0.
     */
    pnv_phb4_xscom_write(qts, PHB_PCIE_DLP_TRWCTL, PPC_BITMASK(0, 63));
    val = pnv_phb4_xscom_read(qts, PHB_PCIE_DLP_TRWCTL);
    g_assert_cmpuint(val & PPC_BIT(1), ==, 0x0);

    /*
     * Set all bits of PHB_LEM_ERROR_AND_MASK, PHB_LEM_ERROR_OR_MASK,
     * both regs are write-only and should be read as 0.
     */
    pnv_phb4_xscom_write(qts, PHB_LEM_ERROR_AND_MASK, PPC_BITMASK(0, 63));
    val = pnv_phb4_xscom_read(qts, PHB_LEM_ERROR_AND_MASK);
    g_assert_cmpuint(val, ==, 0x0);

    pnv_phb4_xscom_write(qts, PHB_LEM_ERROR_OR_MASK, PPC_BITMASK(0, 63));
    val = pnv_phb4_xscom_read(qts, PHB_LEM_ERROR_OR_MASK);
    g_assert_cmpuint(val, ==, 0x0);
}

/* Check that reading an unimplemented address 0x0 returns -1 */
static void phb4_unimplemented_read_test(QTestState *qts)
{
    g_assert_cmpint(pnv_phb4_xscom_read(qts, 0x0), ==, -1);
}

static void test_phb4(void)
{
    QTestState *qts = NULL;

    qts = qtest_initf("-machine powernv10 -accel tcg -nographic -d unimp");

    /* Make sure test is running on PHB */
    phb4_version_test(qts);

    /* Check reset value of a register */
    phb4_reset_test(qts);

    /* Check sticky reset of a register */
    phb4_sticky_rst_test(qts);

    /* Check write-only logic */
    phb4_writeonly_read_test(qts);

    /* Check unimplemented register read */
    phb4_unimplemented_read_test(qts);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("phb4", test_phb4);
    return g_test_run();
}
