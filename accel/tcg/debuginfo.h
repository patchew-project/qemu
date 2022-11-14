/*
 * Debug information support.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_DEBUGINFO_H
#define ACCEL_TCG_DEBUGINFO_H

#if defined(CONFIG_TCG) && defined(CONFIG_LIBDW)
/*
 * Load debuginfo for the specified guest ELF image.
 * Return true on success, false on failure.
 */
bool debuginfo_report_elf(const char *image_name, int image_fd,
                          unsigned long long load_bias);

/*
 * Find a symbol name associated with the specified guest PC.
 * Return true on success, false if there is no associated symbol.
 */
bool debuginfo_get_symbol(unsigned long long address,
                          const char **symbol, unsigned long long *offset);

/*
 * Find a line number associated with the specified guest PC.
 * Return true on success, false if there is no associated line number.
 */
bool debuginfo_get_line(unsigned long long address,
                        const char **file, int *line);
#else
static inline bool debuginfo_report_elf(const char *image_name, int image_fd,
                                        unsigned long long load_bias)
{
    return false;
}

static inline bool debuginfo_get_symbol(unsigned long long address,
                                        const char **symbol,
                                        unsigned long long *offset)
{
    return false;
}

static inline bool debuginfo_get_line(unsigned long long address,
                                      const char **file, int *line)
{
    return false;
}
#endif

#endif
