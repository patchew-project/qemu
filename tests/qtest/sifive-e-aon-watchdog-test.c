#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "libqtest-single.h"
#include "hw/registerfields.h"
#include "hw/misc/sifive_e_aon.h"

FIELD(AON_WDT_WDOGCFG,
      SCALE, 0, 4)
FIELD(AON_WDT_WDOGCFG,
      RSVD0, 4, 4)
FIELD(AON_WDT_WDOGCFG,
      RSTEN, 8, 1)
FIELD(AON_WDT_WDOGCFG,
      ZEROCMP, 9, 1)
FIELD(AON_WDT_WDOGCFG,
      RSVD1, 10, 2)
FIELD(AON_WDT_WDOGCFG,
      EN_ALWAYS, 12, 1)
FIELD(AON_WDT_WDOGCFG,
      EN_CORE_AWAKE, 13, 1)
FIELD(AON_WDT_WDOGCFG,
      RSVD2, 14, 14)
FIELD(AON_WDT_WDOGCFG,
      IP0, 28, 1)
FIELD(AON_WDT_WDOGCFG,
      RSVD3, 29, 3)

#define WDOG_BASE (0x10000000)
#define WDOGCFG (0x0)
#define WDOGCOUNT (0x8)
#define WDOGS (0x10)
#define WDOGFEED (0x18)
#define WDOGKEY (0x1c)
#define WDOGCMP0 (0x20)

#define SIFIVE_E_AON_WDOGKEY (0x51F15E)
#define SIFIVE_E_AON_WDOGFEED (0xD09F00D)
#define SIFIVE_E_LFCLK_DEFAULT_FREQ (32768)

static void test_init(void)
{
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCOUNT, 0);

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, 0);

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCMP0, 0xBEEF);
}

static void test_wdogcount(void)
{
    uint64_t tmp;

    test_init();

    tmp = readl(WDOG_BASE + WDOGCOUNT);
    writel(WDOG_BASE + WDOGCOUNT, 0xBEEF);
    g_assert(readl(WDOG_BASE + WDOGCOUNT) == tmp);

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCOUNT, 0xBEEF);
    g_assert(0xBEEF == readl(WDOG_BASE + WDOGCOUNT));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCOUNT, 0xAAAAAAAA);
    g_assert(0x2AAAAAAA == readl(WDOG_BASE + WDOGCOUNT));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGFEED, 0xAAAAAAAA);
    g_assert(0x2AAAAAAA == readl(WDOG_BASE + WDOGCOUNT));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGFEED, SIFIVE_E_AON_WDOGFEED);
    g_assert(0 == readl(WDOG_BASE + WDOGCOUNT));
}

static void test_wdogcfg(void)
{
    uint32_t tmp_cfg;

    test_init();

    tmp_cfg = readl(WDOG_BASE + WDOGCFG);
    writel(WDOG_BASE + WDOGCFG, 0xFFFFFFFF);
    g_assert(readl(WDOG_BASE + WDOGCFG) == tmp_cfg);

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, 0xFFFFFFFF);
    g_assert(0xFFFFFFFF == readl(WDOG_BASE + WDOGCFG));

    tmp_cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(15 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(1 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(1 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(1 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(1 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(1 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, 0);
    tmp_cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(0 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(0 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(0 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(0 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(0 ==
             FIELD_EX32(tmp_cfg,
                        AON_WDT_WDOGCFG,
                        IP0));
    g_assert(0 == readl(WDOG_BASE + WDOGCFG));
}

static void test_wdogcmp0(void)
{
    uint32_t tmp;

    test_init();

    tmp = readl(WDOG_BASE + WDOGCMP0);
    writel(WDOG_BASE + WDOGCMP0, 0xBEEF);
    g_assert(readl(WDOG_BASE + WDOGCMP0) == tmp);

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCMP0, 0xBEEF);
    g_assert(0xBEEF == readl(WDOG_BASE + WDOGCMP0));
}

static void test_wdogkey(void)
{
    test_init();

    g_assert(0 == readl(WDOG_BASE + WDOGKEY));

    writel(WDOG_BASE + WDOGKEY, 0xFFFF);
    g_assert(0 == readl(WDOG_BASE + WDOGKEY));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    g_assert(1 == readl(WDOG_BASE + WDOGKEY));

    writel(WDOG_BASE + WDOGFEED, 0xAAAAAAAA);
    g_assert(0 == readl(WDOG_BASE + WDOGKEY));
}

static void test_wdogfeed(void)
{
    test_init();

    g_assert(0 == readl(WDOG_BASE + WDOGFEED));

    writel(WDOG_BASE + WDOGFEED, 0xFFFF);
    g_assert(0 == readl(WDOG_BASE + WDOGFEED));
}

static void test_scaled_wdogs(void)
{
    uint32_t cfg;
    uint32_t fake_count = 0x12345678;

    test_init();

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCOUNT, fake_count);
    g_assert(readl(WDOG_BASE + WDOGCOUNT) == fake_count);
    g_assert((uint16_t)readl(WDOG_BASE + WDOGS) ==
             (uint16_t)fake_count);

    for (int i = 0; i < 16; i++) {
        cfg = readl(WDOG_BASE + WDOGCFG);
        cfg = FIELD_DP32(cfg,
                         AON_WDT_WDOGCFG,
                         SCALE, i);
        writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
        writel(WDOG_BASE + WDOGCFG, cfg);
        g_assert((uint16_t)readl(WDOG_BASE + WDOGS) ==
                 (uint16_t)(fake_count >>
                            FIELD_EX32(cfg,
                                       AON_WDT_WDOGCFG,
                                       SCALE)));
    }
}

static void test_watchdog(void)
{
    uint32_t cfg;

    test_init();

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCMP0, SIFIVE_E_LFCLK_DEFAULT_FREQ);

    cfg = readl(WDOG_BASE + WDOGCFG);
    cfg = FIELD_DP32(cfg,
                     AON_WDT_WDOGCFG,
                     SCALE, 0);
    cfg = FIELD_DP32(cfg,
                     AON_WDT_WDOGCFG,
                     EN_ALWAYS, 1);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);

    clock_step(NANOSECONDS_PER_SECOND);

    g_assert(readl(WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ);

    g_assert(readl(WDOG_BASE + WDOGS) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ);

    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCOUNT, 0);
    cfg = FIELD_DP32(cfg,
                     AON_WDT_WDOGCFG,
                     IP0, 0);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);
    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));
}

static void test_scaled_watchdog(void)
{
    uint32_t cfg;

    test_init();

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCMP0, 10);

    cfg = readl(WDOG_BASE + WDOGCFG);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   SCALE, 15);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   EN_ALWAYS, 1);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);

    clock_step(NANOSECONDS_PER_SECOND * 10);

    g_assert(readl(WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ * 10);

    g_assert(10 == readl(WDOG_BASE + WDOGS));

    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(15 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCOUNT, 0);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   IP0, 0);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);
    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));
}

static void test_periodic_int(void)
{
    uint32_t cfg;

    test_init();

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCMP0, SIFIVE_E_LFCLK_DEFAULT_FREQ);

    cfg = readl(WDOG_BASE + WDOGCFG);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   SCALE, 0);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   ZEROCMP, 1);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   EN_ALWAYS, 1);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);

    clock_step(NANOSECONDS_PER_SECOND);

    g_assert(0 == readl(WDOG_BASE + WDOGCOUNT));

    g_assert(0 == readl(WDOG_BASE + WDOGS));

    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    cfg = FIELD_DP32(cfg,
                     AON_WDT_WDOGCFG,
                     IP0, 0);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);
    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    clock_step(NANOSECONDS_PER_SECOND);

    g_assert(0 == readl(WDOG_BASE + WDOGCOUNT));

    g_assert(0 == readl(WDOG_BASE + WDOGS));

    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   IP0, 0);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);
    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));
}

static void test_enable_disable(void)
{
    uint32_t cfg;

    test_init();

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCMP0, 10);

    cfg = readl(WDOG_BASE + WDOGCFG);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   SCALE, 15);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   EN_ALWAYS, 1);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);

    clock_step(NANOSECONDS_PER_SECOND * 2);

    g_assert(readl(WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ * 2);

    g_assert(2 == readl(WDOG_BASE + WDOGS));

    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(15 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   EN_ALWAYS, 0);
    writel(WDOG_BASE + WDOGCFG, cfg);

    clock_step(NANOSECONDS_PER_SECOND * 8);

    g_assert(readl(WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ * 2);

    g_assert(2 == readl(WDOG_BASE + WDOGS));

    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(15 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   EN_ALWAYS, 1);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);

    clock_step(NANOSECONDS_PER_SECOND * 8);

    g_assert(readl(WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ * 10);

    g_assert(10 == readl(WDOG_BASE + WDOGS));

    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(15 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        SCALE));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                         RSTEN));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        ZEROCMP));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_ALWAYS));
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        EN_CORE_AWAKE));
    g_assert(1 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));

    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCOUNT, 0);
    cfg =
        FIELD_DP32(cfg,
                   AON_WDT_WDOGCFG,
                   IP0, 0);
    writel(WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    writel(WDOG_BASE + WDOGCFG, cfg);
    cfg = readl(WDOG_BASE + WDOGCFG);
    g_assert(0 ==
             FIELD_EX32(cfg,
                        AON_WDT_WDOGCFG,
                        IP0));
}

int main(int argc, char *argv[])
{
    int r;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine sifive_e");

    qtest_add_func("/sifive-e-aon-watchdog-test/wdogcount",
                   test_wdogcount);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogcfg",
                   test_wdogcfg);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogcmp0",
                   test_wdogcmp0);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogkey",
                   test_wdogkey);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogfeed",
                   test_wdogfeed);
    qtest_add_func("/sifive-e-aon-watchdog-test/scaled_wdogs",
                   test_scaled_wdogs);
    qtest_add_func("/sifive-e-aon-watchdog-test/watchdog",
                   test_watchdog);
    qtest_add_func("/sifive-e-aon-watchdog-test/scaled_watchdog",
                   test_scaled_watchdog);
    qtest_add_func("/sifive-e-aon-watchdog-test/periodic_int",
                   test_periodic_int);
    qtest_add_func("/sifive-e-aon-watchdog-test/enable_disable",
                   test_enable_disable);

    r = g_test_run();
    qtest_end();

    return r;
}
