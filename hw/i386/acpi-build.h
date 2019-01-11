
#ifndef HW_I386_ACPI_BUILD_H
#define HW_I386_ACPI_BUILD_H

typedef struct memory_range {
    uint64_t base;
    uint64_t length;
    uint32_t node;
} MemoryRange;

extern MemoryRange mem_ranges[];
extern uint32_t mem_ranges_number;

void build_mem_ranges(PCMachineState *pcms);
void acpi_setup(void);

#endif
