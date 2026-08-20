/*
 * QTests for AM64 virt machine
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define OCSRAM_BASE 0x70000000ULL
#define OCSRAM_SIZE (2 * 1024 * 1024)
#define MAIN_UART0_BASE 0x02800000ULL

static void test_ocsram_rw(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    qtest_writel(qts, OCSRAM_BASE, 0xdeadbeef);
    g_assert_cmphex(qtest_readl(qts, OCSRAM_BASE), ==, 0xdeadbeef);
    qtest_writel(qts, OCSRAM_BASE + OCSRAM_SIZE - 4, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, OCSRAM_BASE + OCSRAM_SIZE - 4), ==,
                    0x12345678);
    /* Boot parameters must stay in OCSRAM. */
    qtest_writel(qts, 0x701bebfc, 0x0);
    g_assert_cmphex(qtest_readl(qts, 0x701bebfc), ==, 0x0);
    qtest_quit(qts);
}

static void test_main_uart0_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* Idle 16550: transmitter empty bits are set. */
    g_assert_cmphex(qtest_readl(qts, MAIN_UART0_BASE + (5 << 2)) & 0x60,
                    ==, 0x60);
    qtest_quit(qts);
}

static void test_r5f_cpu_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    QDict *resp = qtest_qmp(qts, "{'execute': 'query-cpus-fast'}");
    QList *cpus = qdict_get_qlist(resp, "return");

    /* 2x A53 + 1x M4 + 1x R5F */
    g_assert_cmpint(qlist_size(cpus), ==, 4);
    qobject_unref(resp);
    qtest_quit(qts);
}

static void test_devstat(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* CTRLMMR_MAIN_DEVSTAT: primary bootmode = eMMC (0x9 << 3). */
    g_assert_cmphex(qtest_readl(qts, 0x43000030), ==, 0x48);
    /* mmr_unlock() kick writes have to be accepted. */
    qtest_writel(qts, 0x43008008, 0x68ef3490);
    qtest_writel(qts, 0x4300800c, 0xd172bc5a);
    qtest_quit(qts);
}

#define DDRSS_CFG_BASE    0x0f308000ULL
#define SP_TARGET(thread) (0x4D000000ULL + (thread) * 0x1000)
#define SP_RT(thread)     (0x4A600000ULL + (thread) * 0x1000)

static void test_dmsc_r5_version(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    /*
     * R5 secure-host TISCI VERSION request.  The zero secure prefix
     * has to be skipped before parsing the TISCI header.
     */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a230002);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    /* Commit with the last data word. */
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);

    /* Response has to land on RX thread 0. */
    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(0)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmpuint(qtest_readl(qts, SP_RT(0)) & 0xff, >, 0);

    /* Secure prefix, echoed VERSION type, ACK bit is set. */
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x08) & 0xffff,
                    ==, 0x0002);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    qtest_quit(qts);
}

static void test_dmsc_r5_get_freq(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /*
     * R5 secure-host GET_FREQ (0x010e) for MMCSD0 clock 1.  This
     * controls packed request layout and nonzero frequency response.
     */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23010e);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 57);
    qtest_writel(qts, SP_TARGET(1) + 0x14, 1);
    /* commit by writing the last data word */
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);

    /* response has to land on RX thread 0 (message count > 0) */
    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(0)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmpuint(qtest_readl(qts, SP_RT(0)) & 0xff, >, 0);

    /* Secure prefix, echoed GET_FREQ type, ACK bit is set. */
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x08) & 0xffff,
                    ==, 0x010e);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    /* freq_hz follows the 8-byte TISCI header and has to be nonzero. */
    g_assert_cmpuint(qtest_readl(qts, SP_TARGET(0) + 0x10), !=, 0);
    qtest_quit(qts);
}

/*
 * Requests without TISCI_MSG_FLAG_AOP must get no reply.  Else a stale
 * message in the single-slot RX thread breaks later request/response
 * pairing, so keep RX count at zero for both no-response messages.
 */
static void test_dmsc_r5_no_response_flag(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /*
     * Drain the reset-time boot notification.  Reading register 15 clears
     * the inbound thread message count again.
     */
    if (qtest_readl(qts, SP_RT(0)) & 0xff) {
        qtest_readl(qts, SP_TARGET(0) + 0x3c);
    }

    /* WAIT_PROC_BOOT_STATUS (0xc401), hdr.flags = 0: no response. */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23c401);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    g_usleep(50 * 1000);
    g_assert_cmphex(qtest_readl(qts, SP_RT(0)) & 0xff, ==, 0);

    /* SET_DEVICE (0x0200), hdr.flags = 0: no response. */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a230200);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 121);   /* device id */
    qtest_writel(qts, SP_TARGET(1) + 0x14, 0);     /* state off */
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    g_usleep(50 * 1000);
    g_assert_cmphex(qtest_readl(qts, SP_RT(0)) & 0xff, ==, 0);

    /* AOP messages still get a response. */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a230002);   /* VERSION */
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(0)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_RT(0)) & 0xff, >, 0);
    qtest_quit(qts);
}

/*
 * TISCI_MSG_SYS_RESET (0x0005) is a no-response request, but it still
 * triggers a full machine reset.
 */
static void test_dmsc_r5_sys_reset(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* Drain the reset-time boot notification. */
    if (qtest_readl(qts, SP_RT(0)) & 0xff) {
        qtest_readl(qts, SP_TARGET(0) + 0x3c);
    }

    /* SYS_RESET from host 35, with secure prefix and hdr.flags = 0. */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a230005);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000000);
    /* commit: write the last data word */
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);

    /* The DMSC reset request appears as QEMU RESET event. */
    qtest_qmp_eventwait(qts, "RESET");
    qtest_quit(qts);
}

static void test_dmtimer_counts(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t t0, t1;

    /* TCLR.ST is safe also when the model free-runs. */
    qtest_writel(qts, 0x02400038, 1);
    t0 = qtest_readl(qts, 0x0240003c);
    qtest_clock_step(qts, 1000000); /* +1 ms */
    t1 = qtest_readl(qts, 0x0240003c);
    /* 20 MHz -> 1 ms = 20000 ticks */
    g_assert_cmpuint(t1 - t0, ==, 20000);
    qtest_quit(qts);
}

static void test_dmtimer_prescaler(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t t0, t1;

    /*
     * PTV=2 with PRE_EN gives 20 MHz / (2 << 2), i.e. 1 ms is
     * 2500 ticks.
     */
    qtest_writel(qts, 0x02400038, 0x2b);
    t0 = qtest_readl(qts, 0x0240003c);
    qtest_clock_step(qts, 1000000); /* +1 ms virtual time */
    t1 = qtest_readl(qts, 0x0240003c);
    g_assert_cmpuint(t1 - t0, ==, 2500);
    qtest_quit(qts);
}

static void test_dmtimer_reconfigure(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t t0, t1;

    /*
     * Changing the prescaler while the timer runs must only affect time
     * after the TCLR write, never rescale already-elapsed ticks.
     */
    qtest_writel(qts, 0x02400038, 1);           /* ST, no prescaler */
    t0 = qtest_readl(qts, 0x0240003c);
    qtest_clock_step(qts, 1000000);             /* +1 ms @ 20 MHz  */
    qtest_writel(qts, 0x02400038, 0x2b);        /* PTV=2, PRE_EN, AR, ST */
    qtest_clock_step(qts, 1000000);             /* +1 ms @ 2.5 MHz */
    t1 = qtest_readl(qts, 0x0240003c);
    g_assert_cmpuint(t1 - t0, ==, 20000 + 2500);
    qtest_quit(qts);
}

#define GICD_BASE 0x01800000ULL
#define GICR_BASE 0x01840000ULL
#define GIC_PIDR2 0xffe8

static void test_gicv3_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* GICD_PIDR2.ArchRev must report a GICv3 distributor. */
    g_assert_cmphex((qtest_readl(qts, GICD_BASE + GIC_PIDR2) >> 4) & 0xf,
                    ==, 3);
    /* first redistributor frame at actual AM64x GICR base */
    g_assert_cmphex((qtest_readl(qts, GICR_BASE + GIC_PIDR2) >> 4) & 0xf,
                    ==, 3);
    qtest_quit(qts);
}

static void test_ddrss_stub(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* DENALI_CTL_0 writes must persist, including dram_class DDR4. */
    qtest_writel(qts, DDRSS_CFG_BASE + 0x0, 0x00000A00);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x0), ==, 0x00000A00);

    /* Done bits are ORed into status reads, also after writes. */
    qtest_writel(qts, DDRSS_CFG_BASE + 0x214C, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x214C) & 0x1, ==, 0x1);
    qtest_writel(qts, DDRSS_CFG_BASE + 0x538, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x538) & (1u << 13),
                    ==, 1u << 13);
    qtest_writel(qts, DDRSS_CFG_BASE + 0x558, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x558) & (1u << 25),
                    ==, 1u << 25);

    /*
     * ECC priming needs both BIST_DONE latches: INT_STATUS_MASTER bit 8
     * and INT_STATUS_BIST bit 0, resp. CTL_341 raw bit 16.
     */
    qtest_writel(qts, DDRSS_CFG_BASE + 0x538, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x538) & (1u << 8),
                    ==, 1u << 8);
    qtest_writel(qts, DDRSS_CFG_BASE + 0x554, 0x0);
    g_assert_cmphex(qtest_readl(qts, DDRSS_CFG_BASE + 0x554) & (1u << 16),
                    ==, 1u << 16);
    qtest_quit(qts);
}

/*
 * SET_CONFIG (0xc100) records the A53 bootvector, and PROC_GET_STATUS
 * (0xc400) must echo it.  The packed payload has processor_id at byte 0
 * and bootvector_low at bytes 1..4, so 0x701c0000 is written as
 * 0x1c000020 / 0x00000070.
 */
static void test_dmsc_r5_bootvector_capture(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t reg4, reg5, bootvector_lo;

    /* drain boot notification pre-queued on thread 0 at reset */
    if (qtest_readl(qts, SP_RT(0)) & 0xff) {
        qtest_readl(qts, SP_TARGET(0) + 0x3c);
    }

    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);      /* sec hdr */
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23c100);      /* hdr */
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);      /* AOP */
    qtest_writel(qts, SP_TARGET(1) + 0x10, 0x1c000020);      /* id+bv */
    qtest_writel(qts, SP_TARGET(1) + 0x14, 0x00000070);
    qtest_writel(qts, SP_TARGET(1) + 0x18, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x1c, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    for (int i = 0; i < 100 && !(qtest_readl(qts, SP_RT(0)) & 0xff); i++) {
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    qtest_readl(qts, SP_TARGET(0) + 0x3c);                    /* drain */

    /* PROC_GET_STATUS (0xc400), proc 32: bootvector_low has to echo. */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0a23c400);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 32);               /* proc_id */
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);
    for (int i = 0; i < 100 && !(qtest_readl(qts, SP_RT(0)) & 0xff); i++) {
        g_usleep(10 * 1000);
    }

    /*
     * Secure responses carry a zero prefix at +0x04.  processor_id is
     * response byte 8, so bootvector_lo starts one byte into register 4
     * and has to be rebuilt from registers 4 and 5.
     */
    reg4 = qtest_readl(qts, SP_TARGET(0) + 0x10);
    reg5 = qtest_readl(qts, SP_TARGET(0) + 0x14);
    bootvector_lo = (reg4 >> 8) | ((reg5 & 0xff) << 24);
    g_assert_cmphex(bootvector_lo, ==, 0x701c0000);
    qtest_quit(qts);
}

#define SDHCI_SD_BASE   0x0fa00000ULL
#define SDHCI_EMMC_BASE 0x0fa10000ULL

static void test_sdhci_present(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /*
     * CAPAB bit 28 advertises 64-bit system-bus support, which is needed
     * for A53 SPL ADMA2-64 transfers.
     */
    g_assert_cmphex(qtest_readl(qts, SDHCI_SD_BASE + 0x40), ==, 0x157c34b4);
    g_assert_cmphex(qtest_readl(qts, SDHCI_EMMC_BASE + 0x40), ==, 0x157c34b4);
    /* Host controller version (0xFE): SDHCI spec 3.00. */
    g_assert_cmphex(qtest_readw(qts, SDHCI_SD_BASE + 0xFE) & 0xff, ==, 2);
    /* PHY window: PHY_STAT1 reads CALDONE|DLLRDY */
    g_assert_cmphex(qtest_readl(qts, 0x0fa08000ULL + 0x130) & 0x3, ==, 0x3);
    g_assert_cmphex(qtest_readl(qts, 0x0fa18000ULL + 0x130) & 0x3, ==, 0x3);
    qtest_quit(qts);
}

#define TRNG_BASE 0x40910000ULL

static void test_trng_stub(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    /* readiness bit is permanently set */
    g_assert_cmphex(qtest_readl(qts, TRNG_BASE + 0x10) & 0x1, ==, 0x1);
    /* Output words must be nonzero and change between reads. */
    uint32_t a = qtest_readl(qts, TRNG_BASE + 0x00);
    uint32_t b = qtest_readl(qts, TRNG_BASE + 0x00);

    g_assert_cmpuint(a, !=, 0);
    g_assert_true(a != b || qtest_readl(qts, TRNG_BASE + 0x04) != a);
    /* INTACK writes are accepted. */
    qtest_writel(qts, TRNG_BASE + 0x10, 0x1);
    /* CONTROL is RAM-backed: read back actual written value */
    qtest_writel(qts, TRNG_BASE + 0x14, 0x400);
    g_assert_cmphex(qtest_readl(qts, TRNG_BASE + 0x14), ==, 0x400);
    qtest_quit(qts);
}

/*
 * A53_0 uses threads 9/8 with the same secure prefix as the R5 secure
 * pair.  If it is classified non-secure, the request header shifts and
 * response prefix is missing, leaving BL31 waiting forever.
 */
static void test_dmsc_a53_secure_version(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    if (qtest_readl(qts, SP_RT(8)) & 0xff) {
        qtest_readl(qts, SP_TARGET(8) + 0x3c);
    }

    /*
     * A53_0 secure-host VERSION request: zero secure prefix, host 10,
     * and AOP is set.
     */
    qtest_writel(qts, SP_TARGET(9) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(9) + 0x08, 0x010a0002);
    qtest_writel(qts, SP_TARGET(9) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(9) + 0x3c, 0x00000000);

    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(8)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_RT(8)) & 0xff, >, 0);

    /* VERSION type echoed and ACK bit is set. */
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(8) + 0x08) & 0xffff,
                    ==, 0x0002);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(8) + 0x0c) & 0x2, ==, 0x2);
    qtest_quit(qts);
}

/*
 * TISCI_MSG_FWL_SET (0x9000) is a bare-ACK request.  Without handler
 * it falls through to the unknown-message NAK path.
 */
static void test_dmsc_fwl_set_ack(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");

    if (qtest_readl(qts, SP_RT(0)) & 0xff) {
        qtest_readl(qts, SP_TARGET(0) + 0x3c);
    }

    /*
     * R5 secure FWL_SET: type 0x9000, host 35, AOP set, fwl_id 0x23,
     * region 3, one permission register.
     */
    qtest_writel(qts, SP_TARGET(1) + 0x04, 0x00000000);
    qtest_writel(qts, SP_TARGET(1) + 0x08, 0x0b239000);
    qtest_writel(qts, SP_TARGET(1) + 0x0c, 0x00000002);
    qtest_writel(qts, SP_TARGET(1) + 0x10, 0x00030023);
    qtest_writel(qts, SP_TARGET(1) + 0x14, 0x00000001);
    qtest_writel(qts, SP_TARGET(1) + 0x3c, 0x00000000);

    for (int i = 0; i < 100; i++) {
        if (qtest_readl(qts, SP_RT(0)) & 0xff) {
            break;
        }
        g_usleep(10 * 1000);
    }
    g_assert_cmphex(qtest_readl(qts, SP_RT(0)) & 0xff, >, 0);

    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x08) & 0xffff,
                    ==, 0x9000);
    g_assert_cmphex(qtest_readl(qts, SP_TARGET(0) + 0x0c) & 0x2, ==, 0x2);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/am64-virt/ocsram", test_ocsram_rw);
    qtest_add_func("/am64-virt/main-uart0", test_main_uart0_present);
    qtest_add_func("/am64-virt/r5f-present", test_r5f_cpu_present);
    qtest_add_func("/am64-virt/devstat", test_devstat);
    qtest_add_func("/am64-virt/dmsc-r5-version", test_dmsc_r5_version);
    qtest_add_func("/am64-virt/dmsc-r5-get-freq", test_dmsc_r5_get_freq);
    qtest_add_func("/am64-virt/dmsc-no-response",
                   test_dmsc_r5_no_response_flag);
    qtest_add_func("/am64-virt/dmsc-sys-reset", test_dmsc_r5_sys_reset);
    qtest_add_func("/am64-virt/dmsc-bootvector",
                   test_dmsc_r5_bootvector_capture);
    qtest_add_func("/am64-virt/dmtimer", test_dmtimer_counts);
    qtest_add_func("/am64-virt/dmtimer-prescaler", test_dmtimer_prescaler);
    qtest_add_func("/am64-virt/dmtimer-reconfigure", test_dmtimer_reconfigure);
    qtest_add_func("/am64-virt/gicv3", test_gicv3_present);
    qtest_add_func("/am64-virt/ddrss-stub", test_ddrss_stub);
    qtest_add_func("/am64-virt/sdhci", test_sdhci_present);
    qtest_add_func("/am64-virt/trng", test_trng_stub);
    qtest_add_func("/am64-virt/dmsc-a53-secure-version",
                   test_dmsc_a53_secure_version);
    qtest_add_func("/am64-virt/dmsc-fwl-set", test_dmsc_fwl_set_ack);
    return g_test_run();
}
