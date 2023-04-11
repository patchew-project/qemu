/*
 * Copyright (c) 2023 Warner Losh <imp@bsdimp.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * OS-Specific portion of syscall_defs.h
 */

#include "freebsd/syscall_nr.h"

/*
 * FreeBSD uses a 64bits time_t except on i386 so we have to add a special case
 * here.
 */
#if (!defined(TARGET_I386))
typedef int64_t target_time_t;
#else
typedef int32_t target_time_t;
#endif

typedef abi_long target_suseconds_t;
