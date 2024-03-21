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
     * Updated value will be copied to reg PHB_PBL_ERR_STATUS.
     *
     * Reset PBL core by setting PHB_PCIE_CRESET_PBL in reg PHB_PCIE_CRESET.
     * Verify the sticky bits are still set.
     */
    pnv_phb4_xscom_write(qts, PHB_PBL_ERR_INJECT, PPC_BITMASK(0, 63));
    pnv_phb4_xscom_write(qts, PHB_PCIE_CRESET, PHB_PCIE_CRESET_PBL); /*Reset*/
    val = pnv_phb4_xscom_read(qts, PHB_PBL_ERR_STATUS);
    g_assert_cmpuint(val, ==, (PPC_BITMASK(0, 9) | PPC_BITMASK(12, 63)));
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

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("phb4", test_phb4);
    return g_test_run();
}
