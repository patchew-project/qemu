/*
 * Sparc cpu parameters for qemu.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef SPARC_CPU_PARAM_H
#define SPARC_CPU_PARAM_H

#ifdef CONFIG_USER_ONLY
# define TARGET_PAGE_BITS_VARY
#elif defined(ifdef TARGET_SPARC64)
# define TARGET_PAGE_BITS 13 /* 8k */
#else
# define TARGET_PAGE_BITS 12 /* 4k */
#endif

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
# define TARGET_VIRT_ADDR_SPACE_BITS 44
#else
# define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

#endif
