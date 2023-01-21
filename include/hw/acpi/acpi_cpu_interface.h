#ifndef ACPI_CPU_INTERFACE_H
#define ACPI_CPU_INTERFACE_H

#include "qom/object.h"
#include "hw/boards.h"
#include "hw/qdev-core.h"

#define TYPE_ACPI_CPU_AML_IF "acpi-cpu-aml-interface"

typedef struct AcpiCpuAmlIfClass AcpiCpuAmlIfClass;
DECLARE_CLASS_CHECKERS(AcpiCpuAmlIfClass, ACPI_CPU_AML_IF,
                       TYPE_ACPI_CPU_AML_IF)
#define ACPI_CPU_AML_IF(obj) \
    INTERFACE_CHECK(AcpiCpuAmlIf, (obj), TYPE_ACPI_CPU_AML_IF)

typedef struct AcpiCpuAmlIf AcpiCpuAmlIf;

struct AcpiCpuAmlIfClass {
    /* <private> */
    InterfaceClass parent_class;

    /* <public> */
    void (*madt_cpu)(int uid, const CPUArchIdList *apic_ids, GArray *entry,
                     bool force_enabled);
};
#endif
