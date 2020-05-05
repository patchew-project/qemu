#ifndef HW_I386_ACPI_COMMON_H
#define HW_I386_ACPI_COMMON_H

#include "include/hw/acpi/acpi-defs.h"
#include "include/hw/acpi/acpi_dev_interface.h"
#include "include/hw/acpi/aml-build.h"
#include "include/hw/acpi/bios-linker-loader.h"
#include "include/hw/i386/x86.h"

/* These are used to size the ACPI tables for -M pc-i440fx-1.7 and
 * -M pc-i440fx-2.0.  Even if the actual amount of AML generated grows
 * a little bit, there should be plenty of free space since the DSDT
 * shrunk by ~1.5k between QEMU 2.0 and QEMU 2.1.
 */
#define ACPI_BUILD_LEGACY_CPU_AML_SIZE    97
#define ACPI_BUILD_ALIGN_SIZE             0x1000

#define ACPI_BUILD_TABLE_SIZE             0x20000

/* Default IOAPIC ID */
#define ACPI_BUILD_IOAPIC_ID 0x0

static inline void acpi_align_size(GArray *blob, unsigned align)
{
    /* Align size to multiple of given size. This reduces the chance
     * we need to change size in the future (breaking cross version migration).
     */
    g_array_set_size(blob, ROUND_UP(acpi_data_len(blob), align));
}

void acpi_build_madt(GArray *table_data, BIOSLinker *linker,
                     X86MachineState *x86ms, AcpiDeviceIf *adev,
                     bool has_pci);
void acpi_build_facs(GArray *table_data);
void acpi_init_common_fadt_data(MachineState *ms, Object *o,
                                AcpiFadtData *data);

#endif
