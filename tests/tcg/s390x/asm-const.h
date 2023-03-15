/* SPDX-License-Identifier: GPL-2.0 */
/* Based on linux kernel's arch/s390/include/asm/asm-const.h . */
#ifndef ASM_CONST_H
#define ASM_CONST_H

#ifdef __ASSEMBLY__
#define stringify_in_c(...) __VA_ARGS__
#else
#define __stringify_in_c(...) #__VA_ARGS__
#define stringify_in_c(...) __stringify_in_c(__VA_ARGS__) " "
#endif

#endif
