/*
 * SBSA GWDT Watchdog Action Table (WDAT)
 *
 * Copyright Red Hat, Inc. 2026
 * Author(s): Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/wdat-gwdt.h"
#include "hw/acpi/wdat.h"
#include "hw/watchdog/sbsa_gwdt.h"

#define GWDT_REG(base, reg_offset, reg_width) { \
                 .space_id = AML_AS_SYSTEM_MEMORY, \
                 .address = base + reg_offset, .bit_width = reg_width, \
                 .access_width = AML_DWORD_ACC };

/*
 *   "Hardware Watchdog Timers Design Specification"
 *       https://uefi.org/acpi 'Watchdog Action Table (WDAT)'
 */
void build_gwdt_wdat(GArray *table_data, BIOSLinker *linker, const char *oem_id,
                     const char *oem_table_id, uint64_t rbase, uint64_t cbase,
                     uint64_t freq)
{
    AcpiTable table = { .sig = "WDAT", .rev = 1, .oem_id = oem_id,
                        .oem_table_id = oem_table_id };

    struct AcpiGenericAddress wrr =  GWDT_REG(rbase, 0x0, 32);
    struct AcpiGenericAddress wor_l =  GWDT_REG(cbase, SBSA_GWDT_WOR, 32);
    struct AcpiGenericAddress wcs =  GWDT_REG(cbase, SBSA_GWDT_WCS, 32);

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 0x20, 4); /* Watchdog Header Length */
    build_append_int_noprefix(table_data, 0xff, 2); /* PCI Segment */
    build_append_int_noprefix(table_data, 0xff, 1); /* PCI Bus Number */
    build_append_int_noprefix(table_data, 0xff, 1); /* PCI Device Number */
    build_append_int_noprefix(table_data, 0xff, 1); /* PCI Function Number */
    build_append_int_noprefix(table_data, 0, 3);    /* Reserved */
    /*
     * WDAT spec suports only 1KHz or more coarse watchdog timer,
     * Set resolution to minimum supported 1ms.
     * Before starting watchdog Windows set countdown value to 5min.
     */
    g_assert(freq <= 1000);
    build_append_int_noprefix(table_data, 1, 4);/* Timer Period, ms */
    /*
     * Needs to be more than 4min, otherwise Windows 11 won't start watchdog.
     * Set max to limits to arbitrary max 10min and min to 5sec.
     */
    build_append_int_noprefix(table_data, 600 * freq, 4);/* Maximum Count */
    build_append_int_noprefix(table_data, 5 * freq, 4);  /* Minimum Count */
    /*
     * WATCHDOG_ENABLED
     */
    build_append_int_noprefix(table_data, 0x81, 1); /* Watchdog Flags */
    build_append_int_noprefix(table_data, 0, 3);    /* Reserved */
    /*
     * watchdog instruction entries
     */
    build_append_int_noprefix(table_data, 8, 4);
    /* Action table */
    build_append_wdat_ins(table_data, WDAT_ACTION_QUERY_RUNNING_STATE,
        WDAT_INS_READ_VALUE,
        wcs, 0x1, 0x1);
    build_append_wdat_ins(table_data, WDAT_ACTION_RESET,
        WDAT_INS_WRITE_VALUE,
        wrr, 0x1, 0x7);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_COUNTDOWN_PERIOD,
        WDAT_INS_WRITE_COUNTDOWN,
        wor_l, 0, 0xffffffff);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_RUNNING_STATE,
        WDAT_INS_WRITE_VALUE | WDAT_INS_PRESERVE_REGISTER,
        wcs, 1, 0x00000001);
    build_append_wdat_ins(table_data, WDAT_ACTION_QUERY_STOPPED_STATE,
        WDAT_INS_READ_VALUE,
        wcs, 0x0, 0x00000001);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_STOPPED_STATE,
        WDAT_INS_WRITE_VALUE | WDAT_INS_PRESERVE_REGISTER,
        wcs, 0x0, 0x00000001);
    build_append_wdat_ins(table_data, WDAT_ACTION_QUERY_WATCHDOG_STATUS,
        WDAT_INS_READ_VALUE,
        wcs, 0x4, 0x00000004);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_WATCHDOG_STATUS,
        WDAT_INS_WRITE_VALUE | WDAT_INS_PRESERVE_REGISTER,
        wrr, 0x4, 0x4);

    acpi_table_end(linker, &table);
}
