
#ifndef HW_I386_ACPI_BUILD_INTERNAL_H
#define HW_I386_ACPI_BUILD_INTERNAL_H

#include "hw/acpi/aml-build.h"

/* #define DEBUG_ACPI_BUILD */
#ifdef DEBUG_ACPI_BUILD
#define ACPI_BUILD_DPRINTF(fmt, ...)        \
    do {printf("ACPI_BUILD: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ACPI_BUILD_DPRINTF(fmt, ...)
#endif

typedef
struct AcpiBuildState {
    /* Copy of table in RAM (for patching). */
    MemoryRegion *table_mr;
    /* Is table patched? */
    uint8_t patched;
    void *rsdp;
    MemoryRegion *rsdp_mr;
    MemoryRegion *linker_mr;
} AcpiBuildState;

typedef struct FwCfgTPMConfig {
    uint32_t tpmppi_address;
    uint8_t tpm_version;
    uint8_t tpmppi_version;
} QEMU_PACKED FwCfgTPMConfig;

/* acpi-build-pc.c */
void acpi_build_pc(AcpiBuildTables *tables, MachineState *machine);

#endif
