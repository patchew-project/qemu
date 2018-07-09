/*
 * QTest testcase for SDHCI controllers
 *
 * Written by Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "libqtest.h"
#include "libqos/pci-pc.h"
#include "hw/pci/pci.h"
#include "libqos/qgraph.h"
#include "libqos/sdhci.h"

#define SDHC_CAPAB                      0x40
FIELD(SDHC_CAPAB, BASECLKFREQ,               8, 8); /* since v2 */
FIELD(SDHC_CAPAB, SDMA,                     22, 1);
FIELD(SDHC_CAPAB, SDR,                      32, 3); /* since v3 */
FIELD(SDHC_CAPAB, DRIVER,                   36, 3); /* since v3 */
#define SDHC_HCVER                      0xFE

/**
 * Old sdhci_t structure:
 *
    struct sdhci_t {
        const char *arch, *machine;
        struct {
            uintptr_t addr;
            uint8_t version;
            uint8_t baseclock;
            struct {
                bool sdma;
                uint64_t reg;
            } capab;
        } sdhci;
        struct {
            uint16_t vendor_id, device_id;
        } pci;
    }
 *
 * implemented drivers:
 *
    PC via PCI
        { "x86_64", "pc",
            {-1,         2, 0,  {1, 0x057834b4} },
            .pci = { PCI_VENDOR_ID_REDHAT, PCI_DEVICE_ID_REDHAT_SDHCI } },

    BCM2835
        { "arm",    "raspi2",
            {0x3f300000, 3, 52, {0, 0x052134b4} } },
 *
 * FIXME: the following drivers are missing:
 *
    Exynos4210
        { "arm",    "smdkc210",
            {0x12510000, 2, 0,  {1, 0x5e80080} } },

    i.MX 6
        { "arm",    "sabrelite",
            {0x02190000, 3, 0,  {1, 0x057834b4} } },

    Zynq-7000
        { "arm",    "xilinx-zynq-a9",   Datasheet: UG585 (v1.12.1)
            {0xe0100000, 2, 0,  {1, 0x69ec0080} } },

    ZynqMP
        { "aarch64", "xlnx-zcu102",     Datasheet: UG1085 (v1.7)
            {0xff160000, 3, 0,  {1, 0x280737ec6481} } },
 */

static void check_specs_version(QSDHCI *s, uint8_t version)
{
    uint32_t v;

    v = s->sdhci_readw(s, SDHC_HCVER);
    v &= 0xff;
    v += 1;
    g_assert_cmpuint(v, ==, version);
}

static void check_capab_capareg(QSDHCI *s, uint64_t expec_capab)
{
    uint64_t capab;

    capab = s->sdhci_readq(s, SDHC_CAPAB);
    g_assert_cmphex(capab, ==, expec_capab);
}

static void check_capab_readonly(QSDHCI *s)
{
    const uint64_t vrand = 0x123456789abcdef;
    uint64_t capab0, capab1;

    capab0 = s->sdhci_readq(s, SDHC_CAPAB);
    g_assert_cmpuint(capab0, !=, vrand);

    s->sdhci_writeq(s, SDHC_CAPAB, vrand);
    capab1 = s->sdhci_readq(s, SDHC_CAPAB);
    g_assert_cmpuint(capab1, !=, vrand);
    g_assert_cmpuint(capab1, ==, capab0);
}

static void check_capab_baseclock(QSDHCI *s, uint8_t expec_freq)
{
    uint64_t capab, capab_freq;

    if (!expec_freq) {
        return;
    }
    capab = s->sdhci_readq(s, SDHC_CAPAB);
    capab_freq = FIELD_EX64(capab, SDHC_CAPAB, BASECLKFREQ);
    g_assert_cmpuint(capab_freq, ==, expec_freq);
}

static void check_capab_sdma(QSDHCI *s, bool supported)
{
    uint64_t capab, capab_sdma;

    capab = s->sdhci_readq(s, SDHC_CAPAB);
    capab_sdma = FIELD_EX64(capab, SDHC_CAPAB, SDMA);
    g_assert_cmpuint(capab_sdma, ==, supported);
}

static void check_capab_v3(QSDHCI *s, uint8_t version)
{
    uint64_t capab, capab_v3;

    if (version < 3) {
        /* before v3 those fields are RESERVED */
        capab = s->sdhci_readq(s, SDHC_CAPAB);
        capab_v3 = FIELD_EX64(capab, SDHC_CAPAB, SDR);
        g_assert_cmpuint(capab_v3, ==, 0);
        capab_v3 = FIELD_EX64(capab, SDHC_CAPAB, DRIVER);
        g_assert_cmpuint(capab_v3, ==, 0);
    }
}

static void test_machine(void *obj, void *data)
{
    QSDHCI *s = (QSDHCI *)obj;

    check_specs_version(s, s->props.version);
    check_capab_capareg(s, s->props.capab.reg);
    check_capab_readonly(s);
    check_capab_v3(s, s->props.version);
    check_capab_sdma(s, s->props.capab.sdma);
    check_capab_baseclock(s, s->props.baseclock);
}

static void sdhci_test(void)
{
    qos_add_test("sdhci-test", "sdhci", test_machine);
}

libqos_init(sdhci_test);
