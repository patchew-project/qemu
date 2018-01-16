/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef PPC_TARGET_ELF_H
#define PPC_TARGET_ELF_H
#ifdef TARGET_PPC64
static inline const char *cpu_get_model(uint32_t eflags)
{
    return "POWER8";
}
#else
static inline const char *cpu_get_model(uint32_t eflags)
{
    return "750";
}
#endif
#endif
