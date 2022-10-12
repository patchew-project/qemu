/*
 * Debug information support.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_DEBUGINFO_H
#define ACCEL_TCG_DEBUGINFO_H

#include "exec/cpu-defs.h"

#ifdef CONFIG_LIBDW
/*
 * Load debuginfo for the specified guest ELF image.
 * Return true on success, false on failure.
 */
bool debuginfo_report_elf(const char *image_name, int image_fd,
                          target_ulong load_bias);

/*
 * Find a symbol name associated with the specified guest PC.
 * Return true on success, false if there is no associated symbol.
 */
bool debuginfo_get_symbol(target_ulong address,
                          const char **symbol, target_ulong *offset);

/*
 * Find a line number associated with the specified guest PC.
 * Return true on success, false if there is no associated line number.
 */
bool debuginfo_get_line(target_ulong address,
                        const char **file, int *line);
#else
static inline bool debuginfo_report_elf(const char *image_name, int image_fd,
                                        target_ulong load_bias)
{
    return false;
}

static inline bool debuginfo_get_symbol(target_ulong address,
                                        const char **symbol,
                                        target_ulong *offset)
{
    return false;
}

static inline bool debuginfo_get_line(target_ulong address,
                                      const char **file, int *line)
{
    return false;
}
#endif

#endif
