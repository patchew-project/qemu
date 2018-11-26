/*
 * ACPI Utility Functions
 *
 * Copyright (c) 2013 Red Hat Inc.
 * Copyright (c) 2017 Skyport Systems
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>,
 *  Ben Warren <ben@skyportsystems.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "qemu-common.h"
#include "hw/smbios/smbios.h"
#include "qemu/bitmap.h"
#include "acpi-utils.h"
#include "boot-sector.h"

uint8_t acpi_calc_checksum(const uint8_t *data, int len)
{
    int i;
    uint8_t sum = 0;

    for (i = 0; i < len; i++) {
        sum += data[i];
    }

    return sum;
}

uint32_t acpi_find_rsdp_address(void)
{
    uint32_t off;

    /* RSDP location can vary across a narrow range */
    for (off = 0xf0000; off < 0x100000; off += 0x10) {
        uint8_t sig[] = "RSD PTR ";
        int i;

        for (i = 0; i < sizeof sig - 1; ++i) {
            sig[i] = readb(off + i);
        }

        if (!memcmp(sig, "RSD PTR ", sizeof sig)) {
            break;
        }
    }
    return off;
}

uint32_t acpi_find_rsdt_address(uint8_t *rsdp_table)
{
    uint32_t rsdt_physical_address = *((uint32_t *)(rsdp_table +
                                                    ACPI_RSDP_RSDT_OFFSET));
    uint8_t revision = rsdp_table[ACPI_RSDP_REVISION_OFFSET];

    if (revision != ACPI_RSDP_REV_1) {
        return 0;
    }

    return le32_to_cpu(rsdt_physical_address);
}

uint64_t acpi_find_xsdt_address(uint8_t *rsdp_table)
{
    uint64_t xsdt_physical_address = *((uint64_t *)(rsdp_table +
                                                    ACPI_RSDP_XSDT_OFFSET));
    uint8_t revision = rsdp_table[ACPI_RSDP_REVISION_OFFSET];

    if (revision != ACPI_RSDP_REV_2) {
        return 0;
    }

    return le64_to_cpu(xsdt_physical_address);
}

void acpi_parse_rsdp_table(uint32_t addr, uint8_t *rsdp_table, uint8_t revision)
{
    switch (revision) {
    case ACPI_RSDP_REV_1:
        memread(addr, rsdp_table, ACPI_RSDP_REV_1_LEN);
        break;
    case ACPI_RSDP_REV_2:
        memread(addr, rsdp_table, ACPI_RSDP_REV_2_LEN);
        break;
    default:
        g_assert_not_reached();
    }

    ACPI_ASSERT_CMP64(*((uint64_t *)(rsdp_table)), ACPI_RSDP_SIGNATURE);
}
