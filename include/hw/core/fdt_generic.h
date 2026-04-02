/*
 * SPDX-License-Identifier: MIT
 *
 * Tables of FDT device models and their init functions. Keyed by compatibility
 * strings, device instance names.
 */

#ifndef FDT_GENERIC_H
#define FDT_GENERIC_H

#include "qemu/help-texts.h"
#include "system/device_tree.h"

typedef struct FDTMachineInfo FDTMachineInfo;

typedef int (*FDTInitFn)(char *, FDTMachineInfo *, void *);

/* associate a FDTInitFn with a FDT compatibility */

void add_to_compat_table(FDTInitFn, const char *, void *);

/*
 * try and find a device model for a particular compatibility. If found,
 * the FDTInitFn associated with the compat will be called and 0 will
 * be returned. Returns non-zero on not found or error
 */

int fdt_init_compat(char *, FDTMachineInfo *, const char *);

/* same as above, but associates with a FDT node name (rather than compat) */

void add_to_inst_bind_table(FDTInitFn, const char *, void *);
int fdt_init_inst_bind(char *, FDTMachineInfo *, const char *);

/* statically register a FDTInitFn as being associate with a compatibility */

#define fdt_register_compatibility_opaque(function, compat, n, opaque) \
static void __attribute__((constructor)) \
function ## n ## _register_imp(void) { \
    add_to_compat_table(function, compat, opaque); \
}

#define fdt_register_compatibility_n(function, compat, n) \
fdt_register_compatibility_opaque(function, compat, n, NULL)

#define fdt_register_compatibility(function, compat) \
fdt_register_compatibility_n(function, compat, 0)

#define fdt_register_instance_opaque(function, inst, n, opaque) \
static void __attribute__((constructor)) \
function ## n ## _register_imp(void) { \
    add_to_inst_bind_table(function, inst, opaque); \
}

#define fdt_register_instance_n(function, inst, n) \
fdt_register_instance_opaque(function, inst, n, NULL)

#define fdt_register_instance(function, inst) \
fdt_register_instance_n(function, inst, 0)

#endif /* FDT_GENERIC_H */
