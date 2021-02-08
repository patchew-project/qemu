/*
 * QTest testcase for ACPI ERST
 *
 * Copyright (c) 2021 Oracle
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "qemu/uuid.h"
#include "hw/acpi/acpi-defs.h"
#include "boot-sector.h"
#include "acpi-utils.h"
#include "libqos/libqtest.h"
#include "qapi/qmp/qdict.h"

#define RSDP_ADDR_INVALID 0x100000 /* RSDP must be below this address */

static uint64_t acpi_find_erst(QTestState *qts)
{
    uint32_t rsdp_offset;
    uint8_t rsdp_table[36 /* ACPI 2.0+ RSDP size */];
    uint32_t rsdt_len, table_length;
    uint8_t *rsdt, *ent;
    uint64_t base = 0;

    /* Wait for guest firmware to finish and start the payload. */
    boot_sector_test(qts);

    /* Tables should be initialized now. */
    rsdp_offset = acpi_find_rsdp_address(qts);

    g_assert_cmphex(rsdp_offset, <, RSDP_ADDR_INVALID);

    acpi_fetch_rsdp_table(qts, rsdp_offset, rsdp_table);
    acpi_fetch_table(qts, &rsdt, &rsdt_len, &rsdp_table[16 /* RsdtAddress */],
                     4, "RSDT", true);

    ACPI_FOREACH_RSDT_ENTRY(rsdt, rsdt_len, ent, 4 /* Entry size */) {
        uint8_t *table_aml;
        acpi_fetch_table(qts, &table_aml, &table_length, ent, 4, NULL, true);
        if (!memcmp(table_aml + 16 /* OEM Table ID */, "BXPCERST", 8)) {
            /*
             * Picking up ERST base address from the Register Region
             * specified as part of the first Serialization Instruction
             * Action (which is a Begin Write Operation).
             */
            memcpy(&base, &table_aml[56], 8);
            g_free(table_aml);
            break;
        }
        g_free(table_aml);
    }
    g_free(rsdt);
    return base;
}

static char disk[] = "tests/erst-test-disk-XXXXXX";

#define ERST_CMD()                              \
    "-accel kvm -accel tcg "                    \
    "-drive id=hd0,if=none,file=%s,format=raw " \
    "-device ide-hd,drive=hd0 ", disk

static void erst_get_error_log_address_range(void)
{
    QTestState *qts;
    uint64_t log_address_range = 0;

    qts = qtest_initf(ERST_CMD());

    uint64_t base = acpi_find_erst(qts);
    g_assert(base != 0);

    /* Issue GET_ERROR_LOG_ADDRESS_RANGE command */
    qtest_writel(qts, base + 0, 0xD);
    /* Read GET_ERROR_LOG_ADDRESS_RANGE result */
    log_address_range = qtest_readq(qts, base + 8);\

    /* Check addr_range is offset of base */
    g_assert((base + 16) == log_address_range);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    int ret;

    ret = boot_sector_init(disk);
    if (ret) {
        return ret;
    }

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/erst/get-error-log-address-range",
                   erst_get_error_log_address_range);

    ret = g_test_run();
    boot_sector_cleanup(disk);

    return ret;
}
