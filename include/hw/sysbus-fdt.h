/*
 * Flattened Device Tree alias helpers
 *
 * Copyright (C) 2018 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#ifndef HW_SYSBUS_FDT_H
#define HW_SYSBUS_FDT_H

void type_register_fdt_alias(const char *name, const char *alias);
void type_register_fdt_aliases(const char *name, const char **aliases);

const char *type_resolve_fdt_alias(const char *alias);

#endif /* HW_SYSBUS_FDT_H */
