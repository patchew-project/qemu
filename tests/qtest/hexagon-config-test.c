/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/hexagon/hexagon.h"

#include "hw/hexagon/machine_cfg_v66g_1024.h.inc"
#include "hw/hexagon/machine_cfg_v68n_1024.h.inc"
#include "hw/hexagon/machine_cfg_v79na_1.h.inc"
#include "hw/hexagon/machine_cfg_v81dgb_1.h.inc"
#include "hw/hexagon/machine_cfg_v81qa_1.h.inc"

typedef struct {
    const char *machine;
    const struct hexagon_machine_config *cfg;
    uint32_t subsystem_base;
} HexagonMachineCfg;

static const HexagonMachineCfg hexagon_machines[] = {
    { "V66G_1024", &v66g_1024, 0 },
    { "V68N_1024", &v68n_1024, 0 },
    { "V79NA_1", &v79na_1, 0 },
    { "V81DGB_1", &v81dgb_1, 0 },
    { "V81QA_1", &v81qa_1, 0 },
    { "V81QA_1", &v81qa_1, 0xfca00000 },
};

static void test_config_table(gconstpointer data)
{
    const HexagonMachineCfg *mc = data;
    uint64_t subsystem_base;
    uint32_t subsystem_base_entry;
    g_autofree char *args;

    if (mc->subsystem_base) {
        args = g_strdup_printf("-machine %s,subsystem-base=0x%x", mc->machine,
                               mc->subsystem_base);
    } else {
        args = g_strdup_printf("-machine %s", mc->machine);
    }

    qtest_start(args);

    subsystem_base_entry = readl(mc->cfg->cfgbase +
                                 offsetof(union hexagon_config_table,
                                          subsystem_base));
    subsystem_base = (uint64_t)subsystem_base_entry << 16;
    g_assert_cmphex(subsystem_base, ==,
                    mc->subsystem_base ?: mc->cfg->csr_base);
    writel(subsystem_base + HEXAGON_L2VIC_OFFSET + 0x200, 1);
    g_assert_cmphex(readl(subsystem_base + HEXAGON_L2VIC_OFFSET + 0x100),
                    ==, 1);
    g_assert_cmphex(readl(subsystem_base + HEXAGON_QTIMER_OFFSET + 0x10),
                    ==, 19200000);

    qtest_end();
}

/* The virt machine maps DDR at guest physical address 0. */
static void test_memory_backend(void)
{
    g_autofree char *path = NULL;
    g_autofree char *args = NULL;
    const uint8_t expected[] = { 0x12, 0x34, 0x56, 0x78 };
    uint8_t actual[4];
    int fd = g_file_open_tmp("hexagon-ram-XXXXXX", &path, NULL);

    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, 16 * 1024 * 1024), ==, 0);
    args = g_strdup_printf("-machine virt,memory-backend=ram -m 16M "
                           "-object memory-backend-file,id=ram,size=16M,"
                           "share=on,mem-path=%s", path);
    qtest_start(args);
    /* QEMU has the file open now, so nothing is left behind on failure. */
    unlink(path);
    writel(0x1000, 0x78563412);
    g_assert_cmpint(lseek(fd, 0x1000, SEEK_SET), ==, 0x1000);
    g_assert_cmpint(read(fd, actual, sizeof(actual)), ==, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));
    qtest_end();
    close(fd);
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(hexagon_machines); i++) {
        const HexagonMachineCfg *mc = &hexagon_machines[i];
        g_autofree char *path = g_strdup_printf("/config/%s/%x", mc->machine,
                                                mc->subsystem_base);

        qtest_add_data_func(path, mc, test_config_table);
    }

    qtest_add_func("/config/memory-backend", test_memory_backend);
    return g_test_run();
}
