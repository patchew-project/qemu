/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANY_CPU_PARAM_H
#define ANY_CPU_PARAM_H

#define TARGET_LONG_BITS 64

#define TARGET_PHYS_ADDR_SPACE_BITS 64 /* MAX(targets) */
#define TARGET_VIRT_ADDR_SPACE_BITS 64 /* MAX(targets) */

#define TARGET_PAGE_BITS_VARY
#define TARGET_PAGE_BITS_MIN  10 /* MIN(targets)=ARMv5/ARMv6, ignoring AVR */

#endif
