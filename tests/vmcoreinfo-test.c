/*
 * QTest testcase for VM coreinfo device
 *
 * Copyright (c) 2017 Red Hat, Inc.
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

#define RSDP_ADDR_INVALID 0x100000 /* RSDP must be below this address */
#define RSDP_SLEEP_US     100000   /* Sleep for 100ms between tries */
#define RSDP_TRIES_MAX    100      /* Max total time is 10 seconds */

typedef struct {
    AcpiTableHeader header;
    gchar name_op;
    gchar vcia[4];
    gchar val_op;
    uint32_t vcia_val;
} QEMU_PACKED VmciTable;

static uint32_t acpi_find_vcia(void)
{
    uint32_t off;
    AcpiRsdpDescriptor rsdp_table;
    uint32_t rsdt;
    AcpiRsdtDescriptorRev1 rsdt_table;
    int tables_nr;
    uint32_t *tables;
    AcpiTableHeader ssdt_table;
    VmciTable vmci_table;
    int i;

    /* Tables may take a short time to be set up by the guest */
    for (i = 0; i < RSDP_TRIES_MAX; i++) {
        off = acpi_find_rsdp_address();
        if (off < RSDP_ADDR_INVALID) {
            break;
        }
        g_usleep(RSDP_SLEEP_US);
    }
    g_assert_cmphex(off, <, RSDP_ADDR_INVALID);

    acpi_parse_rsdp_table(off, &rsdp_table);

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
        if (!strncmp((char *)ssdt_table.oem_table_id, "VMCOREIN", 8)) {
            /* the first entry in the table should be VCIA
             * That's all we need
             */
            ACPI_READ_FIELD(vmci_table.name_op, tables[i]);
            g_assert(vmci_table.name_op == 0x08);  /* name */
            ACPI_READ_ARRAY(vmci_table.vcia, tables[i]);
            g_assert(memcmp(vmci_table.vcia, "VCIA", 4) == 0);
            ACPI_READ_FIELD(vmci_table.val_op, tables[i]);
            g_assert(vmci_table.val_op == 0x0C);  /* dword */
            ACPI_READ_FIELD(vmci_table.vcia_val, tables[i]);
            /* The GUID is written at a fixed offset into the fw_cfg file
             * in order to implement the "OVMF SDT Header probe suppressor"
             * see docs/specs/vmgenid.txt for more details
             */
            g_free(tables);
            return vmci_table.vcia_val;
        }
    }
    g_free(tables);
    return 0;
}

static void vmcoreinfo_test(void)
{
    gchar *cmd;
    uint32_t vmci_addr;
    int i;

    cmd = g_strdup_printf("-machine accel=tcg -device vmcoreinfo,id=vmci");
    qtest_start(cmd);

    vmci_addr = acpi_find_vcia();
    g_assert(vmci_addr);

    for (i = 0; i < 4096; i++) {
        /* check the memory region can be read */
        readb(vmci_addr + i);
    }

    qtest_quit(global_qtest);
    g_free(cmd);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/vmcoreinfo/test", vmcoreinfo_test);
    ret = g_test_run();

    return ret;
}
