/*
 * QTest testcase for PowerNV PHB4
 *
 * Copyright (c) 2025, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/pci-host/pnv_phb4_regs.h"
#include "pnv-xscom.h"

#define PPC_BIT(bit)            (0x8000000000000000ULL >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))

#define PHB3_PBCQ_SPCI_ASB_ADDR      0x0
#define PHB3_PBCQ_SPCI_ASB_DATA      0x2

/* Index of PNV_CHIP_POWER10 in pnv_chips[] within "pnv-xscom.h" */
#define PNV_P10_CHIP_INDEX      3
#define PHB4_XSCOM              0x40084800ull

/*
 * Indirect XSCOM write:
 * - Write 'Indirect Address Register' with register-offset to write.
 * - Write 'Indirect Data Register' with the value.
 */
static void pnv_phb_xscom_write(QTestState *qts, const PnvChip *chip,
        uint64_t scom, uint32_t indirect_addr, uint32_t indirect_data,
        uint64_t reg, uint64_t val)
{
    qtest_writeq(qts, pnv_xscom_addr(chip, (scom >> 3) + indirect_addr), reg);
    qtest_writeq(qts, pnv_xscom_addr(chip, (scom >> 3) + indirect_data), val);
}

/*
 * Indirect XSCOM read::
 * - Write 'Indirect Address Register' with register-offset to read.
 * - Read 'Indirect Data Register' to get the value.
 */
static uint64_t pnv_phb_xscom_read(QTestState *qts, const PnvChip *chip,
        uint64_t scom, uint32_t indirect_addr, uint32_t indirect_data,
        uint64_t reg)
{
    qtest_writeq(qts, pnv_xscom_addr(chip, (scom >> 3) + indirect_addr), reg);
    return qtest_readq(qts, pnv_xscom_addr(chip, (scom >> 3) + indirect_data));
}

#define phb4_xscom_write(a, v) pnv_phb_xscom_write(qts, \
                                   &pnv_chips[PNV_P10_CHIP_INDEX], PHB4_XSCOM, \
                                   PHB_SCOM_HV_IND_ADDR, PHB_SCOM_HV_IND_DATA, \
                                   PPC_BIT(0) | a, v)

#define phb4_xscom_read(a) pnv_phb_xscom_read(qts, \
                                   &pnv_chips[PNV_P10_CHIP_INDEX], PHB4_XSCOM, \
                                   PHB_SCOM_HV_IND_ADDR, PHB_SCOM_HV_IND_DATA, \
                                   PPC_BIT(0) | a)

/* Assert that 'PHB PBL Control' register has correct reset value */
static void phb4_reset_test(QTestState *qts)
{
    g_assert_cmpuint(phb4_xscom_read(PHB_PBL_CONTROL), ==, 0xC009000000000000);
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
    phb4_xscom_write(PHB_PBL_ERR_INJECT, PPC_BITMASK(0, 63));
    phb4_xscom_write(PHB_PCIE_CRESET, PHB_PCIE_CRESET_PBL); /*Reset*/
    val = phb4_xscom_read(PHB_PBL_ERR_STATUS);
    g_assert_cmpuint(val, ==, (PPC_BITMASK(0, 9) | PPC_BITMASK(12, 63)));
}

static void phb4_tests(void)
{
    QTestState *qts = NULL;

    qts = qtest_initf("-machine powernv10 -accel tcg");

    /* Check reset value of a register */
    phb4_reset_test(qts);

    /* Check sticky reset of a register */
    phb4_sticky_rst_test(qts);

    qtest_quit(qts);
}

/* Assert that 'PHB - Version Register' bits-[24:31] are as expected */
static void phb_version_test(const void *data)
{
    const PnvChip *chip = (PnvChip *)data;
    QTestState *qts;
    const char *machine = "powernv8";
    uint64_t phb_xscom = 0x4809e000;
    uint64_t reg_phb_version = PHB_VERSION;
    uint32_t indirect_addr = PHB3_PBCQ_SPCI_ASB_ADDR;
    uint32_t indirect_data = PHB3_PBCQ_SPCI_ASB_DATA;
    uint32_t expected_ver = 0xA3;

    if (chip->chip_type == PNV_CHIP_POWER9) {
        machine = "powernv9";
        phb_xscom = 0x68084800;
        indirect_addr = PHB_SCOM_HV_IND_ADDR;
        indirect_data = PHB_SCOM_HV_IND_DATA;
        reg_phb_version |= PPC_BIT(0);
        expected_ver = 0xA4;
    } else if (chip->chip_type == PNV_CHIP_POWER10) {
        machine = "powernv10";
        phb_xscom = PHB4_XSCOM;
        indirect_addr = PHB_SCOM_HV_IND_ADDR;
        indirect_data = PHB_SCOM_HV_IND_DATA;
        reg_phb_version |= PPC_BIT(0);
        expected_ver = 0xA5;
    }

    qts = qtest_initf("-M %s -accel tcg -cpu %s", machine, chip->cpu_model);

    uint64_t ver = pnv_phb_xscom_read(qts, chip, phb_xscom,
                                indirect_addr, indirect_data, reg_phb_version);

    /* PHB Version register bits [24:31] */
    ver = ver >> (63 - 31);
    g_assert_cmpuint(ver, ==, expected_ver);
}

/* Verify versions of all supported PHB's */
static void add_phbX_version_test(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(pnv_chips); i++) {
        char *tname = g_strdup_printf("pnv-phb/%s",
                                      pnv_chips[i].cpu_model);
        qtest_add_data_func(tname, &pnv_chips[i], phb_version_test);
        g_free(tname);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /* PHB[345] tests */
    add_phbX_version_test();

    /* PHB4 specific tests */
    qtest_add_func("phb4", phb4_tests);

    return g_test_run();
}
