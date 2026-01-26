// SPDX-License-Identifier: MIT

#ifndef FDT_GENERIC_UTIL_H
#define FDT_GENERIC_UTIL_H

#include "qemu/help-texts.h"
#include "fdt_generic.h"
#include "system/memory.h"
#include "qom/object.h"

/*
 * create a fdt_generic machine. the top level cpu irqs are required for
 * systems instantiating interrupt devices. The client is responsible for
 * destroying the returned FDTMachineInfo (using fdt_init_destroy_fdti)
 */

FDTMachineInfo *fdt_generic_create_machine(void *fdt, qemu_irq *cpu_irq);

#endif /* FDT_GENERIC_UTIL_H */
