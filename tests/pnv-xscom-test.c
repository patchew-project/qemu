/*
 * QTest testcase for PowerNV XSCOM bus
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"

#include "libqtest.h"

typedef enum PnvChipType {
    PNV_CHIP_POWER8E,     /* AKA Murano (default) */
    PNV_CHIP_POWER8,      /* AKA Venice */
    PNV_CHIP_POWER8NVL,   /* AKA Naples */
    PNV_CHIP_POWER9,      /* AKA Nimbus */
} PnvChipType;

#define PNV_XSCOM_EX_BASE         0x10000000
#define PNV_XSCOM_EX_CORE_BASE(i) (PNV_XSCOM_EX_BASE | (((uint64_t)i) << 24))

static const struct pnv_chip {
    PnvChipType chip_type;
    const char *cpu_model;
    uint64_t    xscom_base;
    uint64_t    cfam_id;
    uint32_t    first_core;
} pnv_chips[] = {
    {
        .chip_type  = PNV_CHIP_POWER8,
        .cpu_model  = "POWER8",
        .xscom_base = 0x003fc0000000000ull,
        .cfam_id    = 0x220ea04980000000ull,
        .first_core = 0x1,
    },
    {
        .chip_type  = PNV_CHIP_POWER8NVL,
        .cpu_model  = "POWER8NVL",
        .xscom_base = 0x003fc0000000000ull,
        .cfam_id    = 0x120d304980000000ull,
        .first_core = 0x1,
    },
    {
        .chip_type  = PNV_CHIP_POWER9,
        .cpu_model  = "POWER9",
        .xscom_base = 0x00603fc00000000ull,
        .cfam_id    = 0x100d104980000000ull,
        .first_core = 0x20,
    },
};

static uint64_t pnv_xscom_addr(const struct pnv_chip *chip, uint32_t pcba)
{
    uint64_t addr = chip->xscom_base;

    if (chip->chip_type == PNV_CHIP_POWER9) {
        return addr | ((uint64_t) pcba << 3);
    } else {
        return addr | (((uint64_t) pcba << 4) & ~0xfful) |
            (((uint64_t) pcba << 3) & 0x78);
    }
}

static void test_xscom_cfam_id(const struct pnv_chip *chip)
{
    uint64_t f000f = readq(pnv_xscom_addr(chip, 0xf000f));

    g_assert_cmphex(f000f, ==, chip->cfam_id);
}

static void test_cfam_id(const void *data)
{
    char *args;
    const struct pnv_chip *chip = data;

    args = g_strdup_printf("-M powernv,accel=tcg -cpu %s", chip->cpu_model);

    qtest_start(args);
    test_xscom_cfam_id(chip);
    qtest_quit(global_qtest);

    g_free(args);
}

static void test_xscom_core(const struct pnv_chip *chip)
{
    uint32_t first_core_dts0 =
        PNV_XSCOM_EX_CORE_BASE(chip->first_core) | 0x50000;
    uint64_t dts0 = readq(pnv_xscom_addr(chip, first_core_dts0));

    g_assert_cmphex(dts0, ==, 0x26f024f023f0000ull);
}

static void test_core(const void *data)
{
    char *args;
    const struct pnv_chip *chip = data;

    args = g_strdup_printf("-M powernv,accel=tcg -cpu %s", chip->cpu_model);

    qtest_start(args);
    test_xscom_core(chip);
    qtest_quit(global_qtest);

    g_free(args);
}

int main(int argc, char **argv)
{
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(pnv_chips); i++) {
        char *name = g_strdup_printf("pnv-xscom/cfam_id/%s",
                                     pnv_chips[i].cpu_model);
        qtest_add_data_func(name, &pnv_chips[i], test_cfam_id);
        g_free(name);
    }

    for (i = 0; i < ARRAY_SIZE(pnv_chips); i++) {
        /*
         * Discard P9 for the moment as EQ/EX/EC XSCOM mapping needs a
         * rework
         */
        if (pnv_chips[i].chip_type == PNV_CHIP_POWER9) {
            continue;
        }

        char *name = g_strdup_printf("pnv-xscom/core/%s",
                                     pnv_chips[i].cpu_model);
        qtest_add_data_func(name, &pnv_chips[i], test_core);
        g_free(name);
    }

    return g_test_run();
}
