/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef TCG_OP_ADDRESS_BITS
#define TCG_OP_ADDRESS_BITS

#ifdef COMPILING_PER_TARGET
 #include "exec/target_long.h"
 #ifndef TARGET_ADDRESS_BITS
  #define TARGET_ADDRESS_BITS TARGET_LONG_BITS
 #endif
#else
 #ifndef TARGET_ADDRESS_BITS
  #error TARGET_ADDRESS_BITS must be defined for current file
 #endif
#endif /* COMPILING_PER_TARGET */

#if TARGET_ADDRESS_BITS != 32 && TARGET_ADDRESS_BITS != 64
 #error TARGET_ADDRESS_BITS must be 32 or 64
#endif

#endif /* TCG_OP_ADDRESS_BITS */
