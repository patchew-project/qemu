/*
 * QTest testcase for a Nuvoton NPCT75x TPM SPI device
 *                      running on the PowerNV machine.
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "libqtest-single.h"
#include "hw/acpi/tpm.h"
#include "hw/pci/pci_ids.h"
#include "qtest_aspeed.h"
#include "tpm-emu.h"
#include "hw/ssi/pnv_spi_regs.h"
#include "pnv-xscom.h"

#define SPI_TPM_BASE            0xc0080
#define SPI_SHIFT_COUNTER_N1    0x30000000
#define SPI_SHIFT_COUNTER_N2    0x40000000
#define SPI_RWX_OPCODE_SHIFT    56
#define SPI_RWX_ADDR_SHIFT      32
#define SPI_CMD_DATA_SHIFT      56

#define CFG_COUNT_COMPARE_1     0x0000000200000000
#define MM_REG_RDR_MATCH        0x00000000ff01ff00
#define SEQ_OP_REG_BASIC        0x1134416200100000

#define TPM_TIS_8BITS_MASK      0xff
#define SPI_TPM_TIS_ADDR        0xd40000
#define SPI_EXTEND              0x03
#define TPM_WRITE_OP            0x0
#define TPM_READ_OP             0x80

#define MAX_RETRIES             4

static const uint8_t TPM_CMD[12] =
                     "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00";

#define DPRINTF(fmt, ...) do { \
    if (DEBUG_TIS_TEST) { \
        printf(fmt, ## __VA_ARGS__); \
    } \
} while (0)

#define DEBUG_TIS_TEST 0

#define DPRINTF_ACCESS \
    DPRINTF("%s: %d: locty=%d l=%d access=0x%02x pending_request_flag=0x%x\n", \
            __func__, __LINE__, locty, l, access, pending_request_flag)

#define DPRINTF_STS \
    DPRINTF("%s: %d: sts = 0x%08x\n", __func__, __LINE__, sts)

static uint64_t pnv_spi_tpm_read(const PnvChip *chip, uint32_t reg)
{
    uint32_t pcba = SPI_TPM_BASE + reg;

    return qtest_readq(global_qtest, pnv_xscom_addr(chip, pcba));
}

static void pnv_spi_tpm_write(const PnvChip *chip,
                              uint32_t reg,
                              uint64_t val)
{
    uint32_t pcba = SPI_TPM_BASE + reg;

    qtest_writeq(global_qtest, pnv_xscom_addr(chip, pcba), val);
}

static void spi_op_complete(const PnvChip *chip)
{
    uint64_t cfg_reg;

    cfg_reg = pnv_spi_tpm_read(chip, SPI_CLK_CFG_REG);
    g_assert_cmpuint(CFG_COUNT_COMPARE_1, ==, cfg_reg);
    pnv_spi_tpm_write(chip, SPI_CLK_CFG_REG, 0);
}

static void spi_write_reg(const PnvChip *chip, uint64_t val)
{
    int i;
    uint64_t spi_sts;

    for (i = 0; i < MAX_RETRIES; i++) {
        spi_sts = pnv_spi_tpm_read(chip, SPI_STS_REG);
        if (GETFIELD(SPI_STS_TDR_FULL, spi_sts) == 1) {
            g_usleep(G_USEC_PER_SEC / 2);
        } else {
            break;
        }
    }
    /* cannot write if SPI_STS_TDR_FULL bit is still set */
    g_assert_cmpuint(0, ==, GETFIELD(SPI_STS_TDR_FULL, spi_sts));
    pnv_spi_tpm_write(chip, SPI_XMIT_DATA_REG, val);

    for (i = 0; i < MAX_RETRIES; i++) {
        spi_sts = pnv_spi_tpm_read(chip, SPI_STS_REG);
        if (GETFIELD(SPI_STS_SHIFTER_FSM, spi_sts) & FSM_DONE) {
            break;
        } else {
            g_usleep(G_USEC_PER_SEC / 100);
        }
    }
    /* it should be done given the amount of time */
    g_assert_cmpuint(0, ==, GETFIELD(SPI_STS_SHIFTER_FSM, spi_sts) & FSM_DONE);
    spi_op_complete(chip);
}

static uint64_t spi_read_reg(const PnvChip *chip)
{
    int i;
    uint64_t spi_sts, val = 0;

    for (i = 0; i < MAX_RETRIES; i++) {
        spi_sts = pnv_spi_tpm_read(chip, SPI_STS_REG);
        if (GETFIELD(SPI_STS_RDR_FULL, spi_sts) == 1) {
            val = pnv_spi_tpm_read(chip, SPI_RCV_DATA_REG);
            break;
        }
        g_usleep(G_USEC_PER_SEC / 2);
    }
    for (i = 0; i < MAX_RETRIES; i++) {
        spi_sts = pnv_spi_tpm_read(chip, SPI_STS_REG);
        if (GETFIELD(SPI_STS_RDR_FULL, spi_sts) == 1) {
            g_usleep(G_USEC_PER_SEC / 10);
        } else {
            break;
        }
    }
    /* SPI_STS_RDR_FULL bit should be reset after read */
    g_assert_cmpuint(0, ==, GETFIELD(SPI_STS_RDR_FULL, spi_sts));
    spi_op_complete(chip);
    return val;
}

static void spi_access_start(const PnvChip *chip,
                             bool n2,
                             uint8_t bytes,
                             uint8_t tpm_op,
                             uint32_t tpm_reg)
{
    uint64_t cfg_reg;
    uint64_t reg_op;
    uint64_t seq_op = SEQ_OP_REG_BASIC;

    cfg_reg = pnv_spi_tpm_read(chip, SPI_CLK_CFG_REG);
    if (cfg_reg != CFG_COUNT_COMPARE_1) {
        pnv_spi_tpm_write(chip, SPI_CLK_CFG_REG, CFG_COUNT_COMPARE_1);
    }
    /* bytes - sequencer operation register bits 24:31 */
    if (n2) {
        seq_op |= SPI_SHIFT_COUNTER_N2 | (bytes << 0x18);
    } else {
        seq_op |= SPI_SHIFT_COUNTER_N1 | (bytes << 0x18);
    }
    pnv_spi_tpm_write(chip, SPI_SEQ_OP_REG, seq_op);
    pnv_spi_tpm_write(chip, SPI_MM_REG, MM_REG_RDR_MATCH);
    pnv_spi_tpm_write(chip, SPI_CTR_CFG_REG, (uint64_t)0);
    reg_op = ((uint64_t)tpm_op << SPI_RWX_OPCODE_SHIFT) |
             ((uint64_t)tpm_reg << SPI_RWX_ADDR_SHIFT);
    pnv_spi_tpm_write(chip, SPI_XMIT_DATA_REG, reg_op);
}

static inline void tpm_reg_writeb(const PnvChip *c,
                                  uint8_t locty,
                                  uint8_t reg,
                                  uint8_t val)
{
    uint32_t tpm_reg_locty = SPI_TPM_TIS_ADDR |
                             (locty << TPM_TIS_LOCALITY_SHIFT);

    spi_access_start(c, false, 1, TPM_WRITE_OP, tpm_reg_locty | reg);
    spi_write_reg(c, (uint64_t) val << SPI_CMD_DATA_SHIFT);
}

static inline uint8_t tpm_reg_readb(const PnvChip *c,
                                    uint8_t locty,
                                    uint16_t reg)
{
    uint32_t tpm_reg_locty = SPI_TPM_TIS_ADDR |
                             (locty << TPM_TIS_LOCALITY_SHIFT);

    spi_access_start(c, true, 1, TPM_READ_OP, tpm_reg_locty | reg);
    return spi_read_reg(c);
}

static inline void tpm_reg_writel(const PnvChip *c,
                                  uint8_t locty,
                                  uint16_t reg,
                                  uint32_t val)
{
    int i;

    for (i = 0; i < 4; i++) {
        tpm_reg_writeb(c, locty, reg + i, ((val >> (8 * i)) & 0xff));
    }
}

static inline uint32_t tpm_reg_readl(const PnvChip *c,
                                     uint8_t locty,
                                     uint16_t reg)
{
    uint32_t val = 0;
    int i;

    for (i = 0; i < 4; i++) {
        val |= tpm_reg_readb(c, locty, reg + i) << (8 * i);
    }
    return val;
}

static void tpm_set_verify_loc(const PnvChip *chip, uint8_t loc)
{
    uint8_t access;
    uint32_t tpm_sts, capability;

    g_test_message("TPM locality %d tests:", loc);
    access = tpm_reg_readb(chip, loc, TPM_TIS_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    capability = tpm_reg_readl(chip, loc, TPM_TIS_REG_INTF_CAPABILITY);
    g_assert_cmpint(capability, ==, TPM_TIS_CAPABILITIES_SUPPORTED2_0);

    tpm_reg_writeb(chip, loc, TPM_TIS_REG_ACCESS, TPM_TIS_ACCESS_SEIZE);
    tpm_reg_writeb(chip, loc, TPM_TIS_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);

    access = tpm_reg_readb(chip, loc, TPM_TIS_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    g_test_message("\tACCESS REG = 0x%x checked", access);

    /* test tpm status register */
    tpm_sts = tpm_reg_readl(chip, loc, TPM_TIS_REG_STS);
    g_assert_cmpuint((tpm_sts & TPM_TIS_8BITS_MASK), ==, 0);
    g_test_message("\tTPM STATUS: 0x%x, verified", tpm_sts);

    /* release access */
    tpm_reg_writeb(chip, loc, TPM_TIS_REG_ACCESS,
                   TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = tpm_reg_readb(chip, loc, TPM_TIS_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    g_test_message("\tRELEASED ACCESS: 0x%x, checked", access);
}

static void test_spi_tpm_locality(const void *data)
{
    const PnvChip *chip = &pnv_chips[3];
    uint8_t locality;

    /* Locality 4 has special security restrictions, testing 0-3 */
    for (locality = 0; locality < TPM_TIS_NUM_LOCALITIES - 1; locality++) {
        tpm_set_verify_loc(chip, locality);
    }
}

static void test_spi_tpm_basic(const void *data)
{
    const PnvChip *chip = &pnv_chips[3];
    uint32_t didvid, tpm_sts, en_int;
    uint8_t access;

    g_test_message("TPM TIS SPI interface basic tests:");
    /* vendor ID and device ID ... check against the known value*/
    didvid = tpm_reg_readl(chip, 0, TPM_TIS_REG_DID_VID);
    g_assert_cmpint(didvid, ==, (1 << 16) | PCI_VENDOR_ID_IBM);
    g_test_message("\tDID_VID = 0x%x, verified", didvid);

    /* access register, default see TCG TIS Spec (v1.3) table-14 */
    access = tpm_reg_readb(chip, 0, TPM_TIS_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
    g_test_message("\tACCESS REG = 0x%x, checked", access);

    /* interrupt enable register, default see TCG TIS Spec (v1.3) table-19 */
    en_int = tpm_reg_readl(chip, 0, TPM_TIS_REG_INT_ENABLE);
    g_assert_cmpuint(en_int, ==, TPM_TIS_INT_POLARITY_LOW_LEVEL);
    g_test_message("\tINT ENABLE REG: 0x%x, verified", en_int);

    /* status register, default see TCG TIS Spec (v1.3) table-15 */
    tpm_sts = tpm_reg_readl(chip, 0, TPM_TIS_REG_STS);
    /* for no active locality */
    g_assert_cmpuint(tpm_sts, ==, 0xffffffff);
    g_test_message("\tTPM STATUS: 0x%x, verified", tpm_sts);
}

/*
 * Test case for seizing access by a higher number locality
 */
static void test_spi_tpm_access_seize_test(const void *data)
{
    const PnvChip *chip = &pnv_chips[3];
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    g_test_message("TPM TIS SPI access seize tests:");
    /* do not test locality 4 (hw only) */
    for (locty = 0; locty < TPM_TIS_NUM_LOCALITIES - 1; locty++) {
        pending_request_flag = 0;

        access = tpm_reg_readb(chip, locty, TPM_TIS_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        tpm_reg_writeb(chip, locty, TPM_TIS_REG_ACCESS,
                                    TPM_TIS_ACCESS_REQUEST_USE);

        access = tpm_reg_readb(chip, locty, TPM_TIS_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* lower localities cannot seize access */
        for (l = 0; l < locty; l++) {
            /* lower locality is not active */
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to request use from 'l' */
            tpm_reg_writeb(chip, l, TPM_TIS_REG_ACCESS,
                                    TPM_TIS_ACCESS_REQUEST_USE);

            /*
             * requesting use from 'l' was not possible;
             * we must see REQUEST_USE and possibly PENDING_REQUEST
             */
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * locality 'locty' must be unchanged;
             * we must see PENDING_REQUEST
             */
            access = tpm_reg_readb(chip, locty, TPM_TIS_REG_ACCESS);
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            tpm_reg_writeb(chip, l, TPM_TIS_REG_ACCESS, TPM_TIS_ACCESS_SEIZE);
            /* seize from 'l' was not possible */
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* locality 'locty' must be unchanged */
            access = tpm_reg_readb(chip, locty, TPM_TIS_REG_ACCESS);
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * on the next loop we will have a PENDING_REQUEST flag
             * set for locality 'l'
             */
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }

        /*
         * higher localities can 'seize' access but not 'request use';
         * note: this will activate first l+1, then l+2 etc.
         */
        for (l = locty + 1; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            /* try to 'request use' from 'l' */
            tpm_reg_writeb(chip, l, TPM_TIS_REG_ACCESS,
                                    TPM_TIS_ACCESS_REQUEST_USE);

            /*
             * requesting use from 'l' was not possible; we should see
             * REQUEST_USE and may see PENDING_REQUEST
             */
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /*
             * locality 'l-1' must be unchanged; we should always
             * see PENDING_REQUEST from 'l' requesting access
             */
            access = tpm_reg_readb(chip, l - 1, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        TPM_TIS_ACCESS_PENDING_REQUEST |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* try to seize from 'l' */
            tpm_reg_writeb(chip, l, TPM_TIS_REG_ACCESS, TPM_TIS_ACCESS_SEIZE);

            /* seize from 'l' was possible */
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* l - 1 should show that it has BEEN_SEIZED */
            access = tpm_reg_readb(chip, l - 1, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_BEEN_SEIZED |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* clear the BEEN_SEIZED flag and make sure it's gone */
            tpm_reg_writeb(chip, l - 1, TPM_TIS_REG_ACCESS,
                                        TPM_TIS_ACCESS_BEEN_SEIZED);

            access = tpm_reg_readb(chip, l - 1, TPM_TIS_REG_ACCESS);
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }

        /*
         * PENDING_REQUEST will not be set if locty = 0 since all localities
         * were active; in case of locty = 1, locality 0 will be active
         * but no PENDING_REQUEST anywhere
         */
        if (locty <= 1) {
            pending_request_flag = 0;
        }

        /* release access from l - 1; this activates locty - 1 */
        l--;

        access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
        DPRINTF_ACCESS;

        DPRINTF("%s: %d: relinquishing control on l = %d\n",
                __func__, __LINE__, l);
        tpm_reg_writeb(chip, l, TPM_TIS_REG_ACCESS,
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY);

        access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
        DPRINTF_ACCESS;
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    pending_request_flag |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        for (l = locty - 1; l >= 0; l--) {
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

            /* release this locality */
            tpm_reg_writeb(chip, l, TPM_TIS_REG_ACCESS,
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY);

            if (l == 1) {
                pending_request_flag = 0;
            }
        }

        /* no locality may be active now */
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
        g_test_message("\tTPM locality %d seize tests: passed", locty);
    }
}

/*
 * Test case for getting access when higher number locality relinquishes access
 */
static void test_spi_tpm_access_release_test(const void *data)
{
    const PnvChip *chip = &pnv_chips[3];
    int locty, l;
    uint8_t access;
    uint8_t pending_request_flag;

    g_test_message("TPM TIS SPI access release tests:");
    /* do not test locality 4 (hw only) */
    for (locty = TPM_TIS_NUM_LOCALITIES - 2; locty >= 0; locty--) {
        pending_request_flag = 0;

        access = tpm_reg_readb(chip, locty, TPM_TIS_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of locality */
        tpm_reg_writeb(chip, locty, TPM_TIS_REG_ACCESS,
                                    TPM_TIS_ACCESS_REQUEST_USE);
        access = tpm_reg_readb(chip, locty, TPM_TIS_REG_ACCESS);
        g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                    TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

        /* request use of all other localities */
        for (l = 0; l < TPM_TIS_NUM_LOCALITIES - 1; l++) {
            if (l == locty) {
                continue;
            }
            /*
             * request use of locality 'l' -- we MUST see REQUEST USE and
             * may see PENDING_REQUEST
             */
            tpm_reg_writeb(chip, l, TPM_TIS_REG_ACCESS,
                                    TPM_TIS_ACCESS_REQUEST_USE);
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_REQUEST_USE |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            pending_request_flag = TPM_TIS_ACCESS_PENDING_REQUEST;
        }
        /* release locality 'locty' */
        tpm_reg_writeb(chip, locty, TPM_TIS_REG_ACCESS,
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY);
        /*
         * highest locality should now be active; release it and make sure the
         * next highest locality is active afterwards
         */
        for (l = TPM_TIS_NUM_LOCALITIES - 2; l >= 0; l--) {
            if (l == locty) {
                continue;
            }
            /* 'l' should be active now */
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
            /* 'l' relinquishes access */
            tpm_reg_writeb(chip, l, TPM_TIS_REG_ACCESS,
                                    TPM_TIS_ACCESS_ACTIVE_LOCALITY);
            access = tpm_reg_readb(chip, l, TPM_TIS_REG_ACCESS);
            DPRINTF_ACCESS;
            if (l == 1 || (locty <= 1 && l == 2)) {
                pending_request_flag = 0;
            }
            g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                        pending_request_flag |
                                        TPM_TIS_ACCESS_TPM_ESTABLISHMENT);
        }
        g_test_message("\tTPM locality %d seize tests: passed", locty);
    }
}

/*
 * Test case for transmitting packets
 */
static void test_spi_tpm_transmit_test(const void *data)
{
    const struct TPMTestState *s = data;
    const PnvChip *chip = &pnv_chips[3];
    uint16_t bcount;
    uint8_t access;
    uint32_t sts;
    int i;

    g_test_message("TPM TIS SPI transmit tests:");
    /* request use of locality 0 */
    tpm_reg_writeb(chip, 0, TPM_TIS_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);
    access = tpm_reg_readb(chip, 0, TPM_TIS_REG_ACCESS);
    g_assert_cmpint(access, ==, TPM_TIS_ACCESS_TPM_REG_VALID_STS |
                                TPM_TIS_ACCESS_ACTIVE_LOCALITY |
                                TPM_TIS_ACCESS_TPM_ESTABLISHMENT);

    sts = tpm_reg_readl(chip, 0, TPM_TIS_REG_STS);
    DPRINTF_STS;

    g_assert_cmpint(sts & 0xff, ==, 0);
    g_assert_cmpint(sts & TPM_TIS_STS_TPM_FAMILY_MASK, ==,
                    TPM_TIS_STS_TPM_FAMILY2_0);

    bcount = (sts >> 8) & 0xffff;
    g_test_message("\t\tbcount: %x, sts: %x", bcount, sts);
    g_assert_cmpint(bcount, >=, 128);

    tpm_reg_writel(chip, 0, TPM_TIS_REG_STS, TPM_TIS_STS_COMMAND_READY);
    sts = tpm_reg_readl(chip, 0, TPM_TIS_REG_STS);
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_COMMAND_READY);

    /* transmit command */
    for (i = 0; i < sizeof(TPM_CMD); i++) {
        tpm_reg_writeb(chip, 0, TPM_TIS_REG_DATA_FIFO, TPM_CMD[i]);
        sts = tpm_reg_readl(chip, 0, TPM_TIS_REG_STS);
        DPRINTF_STS;
        if (i < sizeof(TPM_CMD) - 1) {
            g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_EXPECT |
                                            TPM_TIS_STS_VALID);
        } else {
            g_assert_cmpint(sts & 0xff, ==, TPM_TIS_STS_VALID);
        }
        /* since STS is read byte-by-byte bcount will be constant 0xff */
        g_assert_cmpint((sts >> 8) & 0xffff, ==, 0xff);
    }
    g_test_message("\ttransmit tests, check TPM_TIS_STS_EXPECT");

    /* start processing */
    tpm_reg_writel(chip, 0, TPM_TIS_REG_STS, TPM_TIS_STS_TPM_GO);

    uint64_t end_time = g_get_monotonic_time() + 50 * G_TIME_SPAN_SECOND;
    do {
        sts = tpm_reg_readl(chip, 0, TPM_TIS_REG_STS);
        if ((sts & TPM_TIS_STS_DATA_AVAILABLE) != 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);

    sts = tpm_reg_readl(chip, 0, TPM_TIS_REG_STS);
    DPRINTF_STS;
    g_assert_cmpint(sts & 0xff, == , TPM_TIS_STS_VALID |
                                     TPM_TIS_STS_DATA_AVAILABLE);
    /* TCG TIS Spec (v1.3) table-15 */
    g_test_message("\ttransmit tests, check tpmGo (w) & dataAvail (r)");
    bcount = (sts >> 8) & 0xffff;

    /* read response */
    uint8_t tpm_msg[sizeof(struct tpm_hdr)];
    g_assert_cmpint(sizeof(tpm_msg), ==, bcount);

    for (i = 0; i < sizeof(tpm_msg); i++) {
        tpm_msg[i] = tpm_reg_readb(chip, 0, TPM_TIS_REG_DATA_FIFO);
        sts = tpm_reg_readl(chip, 0, TPM_TIS_REG_STS);
        DPRINTF_STS;
        if (sts & TPM_TIS_STS_DATA_AVAILABLE) {
            g_assert_cmpint((sts >> 8) & 0xffff, ==, --bcount);
        }
    }
    g_assert_cmpmem(tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));

    g_test_message("\treceive tests, passed");
    /* relinquish use of locality 0 */
    tpm_reg_writeb(chip, 0, TPM_TIS_REG_ACCESS, TPM_TIS_ACCESS_ACTIVE_LOCALITY);
    access = tpm_reg_readb(chip, 0, TPM_TIS_REG_ACCESS);
}

int main(int argc, char **argv)
{
    int ret;
    char *args;
    GThread *thread;
    TPMTestState test;
    g_autofree char *tmp_path = g_dir_make_tmp("qemu-tpm-tis-spi-test.XXXXXX",
                                                NULL);

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);
    test.data_cond_signal = false;
    test.tpm_version = TPM_VERSION_2_0;

    thread = g_thread_new(NULL, tpm_emu_ctrl_thread, &test);
    tpm_emu_test_wait_cond(&test);

    args = g_strdup_printf("-m 2G -machine powernv10 -nographic "
                      "-chardev socket,id=chrtpm,path=%s "
                      "-tpmdev emulator,id=tpm0,chardev=chrtpm "
                      "-device tpm-tis-spi,tpmdev=tpm0,bus=pnv-spi-bus.4",
                      test.addr->u.q_unix.path);
    qtest_start(args);
    qtest_add_data_func("pnv-xscom/tpm-tis-spi/basic_test",
                        &test, test_spi_tpm_basic);
    qtest_add_data_func("pnv-xscom/tpm-tis-spi/locality_test",
                        &test, test_spi_tpm_locality);
    qtest_add_data_func("pnv-xscom/tpm-tis-spi/access_seize_test",
                        &test, test_spi_tpm_access_seize_test);
    qtest_add_data_func("pnv-xscom/tpm-tis-spi/access_release_test",
                        &test, test_spi_tpm_access_release_test);
    qtest_add_data_func("pnv-xscom/tpm-tis-spi/data_transmit_test",
                        &test, test_spi_tpm_transmit_test);
    ret = g_test_run();

    qtest_end();
    g_thread_join(thread);
    g_unlink(test.addr->u.q_unix.path);
    qapi_free_SocketAddress(test.addr);
    g_rmdir(tmp_path);
    g_free(args);
    return ret;
}

