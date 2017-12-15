/*
 * QTest testcase for SDHCI controllers
 *
 * Written by Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "libqtest.h"

#define SDHC_CAPAB                      0x40
FIELD(SDHC_CAPAB, BASECLKFREQ,               8, 8); /* since v2 */
FIELD(SDHC_CAPAB, SDMA,                     22, 1);
FIELD(SDHC_CAPAB, SDR,                      32, 3); /* since v3 */
FIELD(SDHC_CAPAB, DRIVER,                   36, 3); /* since v3 */
#define SDHC_HCVER                      0xFE

static const struct sdhci_t {
    const char *arch;
    const char *machine;
    struct {
        uintptr_t addr;
        uint8_t version;
        uint8_t baseclock;
        struct {
            bool sdma;
        } capab;
    } sdhci;
} models[] = {
    { "arm",    "smdkc210",
        {0x12510000, 2, 0, {1} } },
    { "arm",    "sabrelite",
        {0x02190000, 3, 0, {1} } },
    { "arm",    "raspi2",           /* bcm2835 */
        {0x3f300000, 3, 52, {0} } },
    { "arm",    "xilinx-zynq-a9",   /* exynos4210 */
        {0xe0100000, 3, 0, {1} } },
};

static uint32_t sdhci_readl(uintptr_t base, uint32_t reg_addr)
{
    QTestState *qtest = global_qtest;

    return qtest_readl(qtest, base + reg_addr);
}

static uint64_t sdhci_readq(uintptr_t base, uint32_t reg_addr)
{
    QTestState *qtest = global_qtest;

    return qtest_readq(qtest, base + reg_addr);
}

static void sdhci_writeq(uintptr_t base, uint32_t reg_addr, uint64_t value)
{
    QTestState *qtest = global_qtest;

    qtest_writeq(qtest, base + reg_addr, value);
}

static void check_specs_version(uintptr_t addr, uint8_t version)
{
    uint32_t v;

    v = sdhci_readl(addr, SDHC_HCVER);
    v &= 0xff;
    v += 1;
    g_assert_cmpuint(v, ==, version);
}

static void check_capab_readonly(uintptr_t addr)
{
    const uint64_t vrand = 0x123456789abcdef;
    uint64_t capab0, capab1;

    capab0 = sdhci_readq(addr, SDHC_CAPAB);
    g_assert_cmpuint(capab0, !=, vrand);

    sdhci_writeq(addr, SDHC_CAPAB, vrand);
    capab1 = sdhci_readq(addr, SDHC_CAPAB);
    g_assert_cmpuint(capab1, !=, vrand);
    g_assert_cmpuint(capab1, ==, capab0);
}

static void check_capab_baseclock(uintptr_t addr, uint8_t expected_freq)
{
    uint64_t capab, capab_freq;

    if (!expected_freq) {
        return;
    }
    capab = sdhci_readq(addr, SDHC_CAPAB);
    capab_freq = FIELD_EX64(capab, SDHC_CAPAB, BASECLKFREQ);
    g_assert_cmpuint(capab_freq, ==, expected_freq);
}

static void check_capab_sdma(uintptr_t addr, bool supported)
{
    uint64_t capab, capab_sdma;

    capab = sdhci_readq(addr, SDHC_CAPAB);
    capab_sdma = FIELD_EX64(capab, SDHC_CAPAB, SDMA);
    g_assert_cmpuint(capab_sdma, ==, supported);
}

static void check_capab_v3(uintptr_t addr, uint8_t version)
{
    uint64_t capab, capab_v3;

    if (version >= 3) {
        return;
    }
    /* before v3 those fields are RESERVED */
    capab = sdhci_readq(addr, SDHC_CAPAB);
    capab_v3 = FIELD_EX64(capab, SDHC_CAPAB, SDR);
    g_assert_cmpuint(capab_v3, ==, 0);
    capab_v3 = FIELD_EX64(capab, SDHC_CAPAB, DRIVER);
    g_assert_cmpuint(capab_v3, ==, 0);
}

static void test_machine(const void *data)
{
    const struct sdhci_t *test = data;

    global_qtest = qtest_startf("-machine %s -d unimp", test->machine);

    check_specs_version(test->sdhci.addr, test->sdhci.version);
    check_capab_readonly(test->sdhci.addr);
    check_capab_v3(test->sdhci.addr, test->sdhci.version);
    check_capab_sdma(test->sdhci.addr, test->sdhci.capab.sdma);
    check_capab_baseclock(test->sdhci.addr, test->sdhci.baseclock);

    qtest_quit(global_qtest);
}

int main(int argc, char *argv[])
{
    char *name;
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(models); i++) {
        name = g_strdup_printf("sdhci/%s", models[i].machine);
        qtest_add_data_func(name, &models[i], test_machine);
        g_free(name);
    }

    return g_test_run();
}
