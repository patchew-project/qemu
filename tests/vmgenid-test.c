/*
 * QTest testcase for VM Generation ID
 *
 * Copyright (c) 2016 Red Hat, Inc.
 * Copyright (c) 2017 Skyport Systems
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include <unistd.h>
#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "qemu/uuid.h"
#include "hw/acpi/acpi-defs.h"
#include "acpi-utils.h"
#include "libqtest.h"

#define VGID_GUID "324e6eaf-d1d1-4bf6-bf41-b9bb6c91fb87"
#define VMGENID_GUID_OFFSET      40   /* allow space for
                                       * OVMF SDT Header Probe Supressor
                                       */

static uint32_t vgia;

typedef struct {
    AcpiTableHeader header;
    gchar name_op;
    gchar vgia[4];
    gchar val_op;
    uint32_t vgia_val;
} QEMU_PACKED VgidTable;

static uint32_t find_vgia(void)
{
    uint32_t off;
    AcpiRsdpDescriptor rsdp_table;
    uint32_t rsdt;
    AcpiRsdtDescriptorRev1 rsdt_table;
    int tables_nr;
    uint32_t *tables;
    AcpiTableHeader ssdt_table;
    VgidTable vgid_table;
    int i;

    /* First, find the RSDP */
    for (off = 0xf0000; off < 0x100000; off += 0x10) {
        uint8_t sig[] = "RSD PTR ";

        for (i = 0; i < sizeof sig - 1; ++i) {
            sig[i] = readb(off + i);
        }

        if (!memcmp(sig, "RSD PTR ", sizeof sig)) {
            break;
        }
    }
    g_assert_cmphex(off, <, 0x100000);

    /* Parse the RSDP header so we can find the RSDT */
    ACPI_READ_FIELD(rsdp_table.signature, off);
    ACPI_ASSERT_CMP64(rsdp_table.signature, "RSD PTR ");

    ACPI_READ_FIELD(rsdp_table.checksum, off);
    ACPI_READ_ARRAY(rsdp_table.oem_id, off);
    ACPI_READ_FIELD(rsdp_table.revision, off);
    ACPI_READ_FIELD(rsdp_table.rsdt_physical_address, off);

    rsdt = rsdp_table.rsdt_physical_address;
    /* read the header */
    ACPI_READ_TABLE_HEADER(&rsdt_table, rsdt);
    ACPI_ASSERT_CMP(rsdt_table.signature, "RSDT");

    /* compute the table entries in rsdt */
    tables_nr = (rsdt_table.length - sizeof(AcpiRsdtDescriptorRev1)) /
                sizeof(uint32_t);
    g_assert_cmpint(tables_nr, >, 0);

    /* get the addresses of the tables pointed by rsdt */
    tables = g_new0(uint32_t, tables_nr);
    ACPI_READ_ARRAY_PTR(tables, tables_nr, rsdt);

    for (i = 0; i < tables_nr; i++) {
        ACPI_READ_TABLE_HEADER(&ssdt_table, tables[i]);
        if (!strncmp((char *)ssdt_table.oem_table_id, "VMGENID", 7)) {
            /* the first entry in the table should be VGIA
             * That's all we need
             */
            ACPI_READ_FIELD(vgid_table.name_op, tables[i]);
            g_assert(vgid_table.name_op == 0x08);  /* name */
            ACPI_READ_ARRAY(vgid_table.vgia, tables[i]);
            g_assert(memcmp(vgid_table.vgia, "VGIA", 4) == 0);
            ACPI_READ_FIELD(vgid_table.val_op, tables[i]);
            g_assert(vgid_table.val_op == 0x0C);  /* dword */
            ACPI_READ_FIELD(vgid_table.vgia_val, tables[i]);
            /* The GUID is written at a fixed offset into the fw_cfg file
             * in order to implement the "OVMF SDT Header probe suppressor"
             * see docs/specs/vmgenid.txt for more details
             */
            return vgid_table.vgia_val + VMGENID_GUID_OFFSET;
        }
    }
    return 0;
}

static void vmgenid_read_guid(QemuUUID *guid)
{
    int i;

    if (vgia == 0) {
        vgia = find_vgia();
    }
    g_assert(vgia);

    /* Read the GUID directly from guest memory */
    for (i = 0; i < 16; i++) {
        guid->data[i] = readb(vgia + i);
    }
    /* The GUID is in little-endian format in the guest, while QEMU
     * uses big-endian.  Swap after reading.
     */
    qemu_uuid_bswap(guid);
}

static void vmgenid_test(void)
{
    QemuUUID expected, measured;
    g_assert(qemu_uuid_parse(VGID_GUID, &expected) == 0);
    vmgenid_read_guid(&measured);
    g_assert(memcmp(measured.data, expected.data, sizeof(measured.data)) == 0);
}

static void vmgenid_set_guid_test(void)
{
    QDict *response;
    gchar *cmd;
    QemuUUID expected, measured;
    g_assert(qemu_uuid_parse(VGID_GUID, &expected) == 0);
    /* Change the GUID slightly */
    expected.data[0] += 1;

    cmd = g_strdup_printf("{ 'execute': 'qom-set', 'arguments': { "
                   "'path': '/machine/peripheral/testvgid', "
                   "'property': 'guid', 'value': '%s' } }",
                   qemu_uuid_unparse_strdup(&expected));
    response = qmp(cmd);
    g_assert(qdict_haskey(response, "return"));
    QDECREF(response);

    vmgenid_read_guid(&measured);
    g_assert(memcmp(measured.data, expected.data, sizeof(measured.data)) == 0);
}

static void vmgenid_set_guid_auto_test(void)
{
    QDict *response;
    QemuUUID expected, measured;

    /* Read the initial value */
    vmgenid_read_guid(&expected);

    /* Setting to 'auto' generates a random GUID */
    response = qmp("{ 'execute': 'qom-set', 'arguments': { "
                   "'path': '/machine/peripheral/testvgid', "
                   "'property': 'guid', 'value': 'auto' } }");

    g_assert(qdict_haskey(response, "return"));
    QDECREF(response);

    vmgenid_read_guid(&measured);
    g_assert(memcmp(measured.data, expected.data, sizeof(measured.data)) != 0);
}

int main(int argc, char **argv)
{
    int ret;
    gchar *cmd;

    g_test_init(&argc, &argv, NULL);

    cmd = g_strdup_printf("-machine accel=tcg -device vmgenid,id=testvgid,"
                          "guid=%s", VGID_GUID);
    qtest_start(cmd);
    qtest_add_func("/vmgenid/vmgenid", vmgenid_test);
    qtest_add_func("/vmgenid/vmgenid/set-guid", vmgenid_set_guid_test);
    qtest_add_func("/vmgenid/vmgenid/set-guid-auto",
                   vmgenid_set_guid_auto_test);
    ret = g_test_run();

    qtest_end();

    return ret;
}
