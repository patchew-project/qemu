/*
 * Stub for pc_build_mem_ranges().
 * piix4 is used not only pc, but also mips and etc. In order to add
 * build_mem_ranges callback to AcpiDeviceIfClass and use pc_build_mem_ranges
 * in hw/acpi/piix4.c, pc_build_mem_ranges() stub is added to make other arch
 * can compile successfully.
 */

#include "qemu/osdep.h"
#include "hw/i386/pc.h"

void pc_build_mem_ranges(AcpiDeviceIf *adev, MachineState *ms)
{
}
