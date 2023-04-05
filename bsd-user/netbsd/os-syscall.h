/*
 * Copyright (c) 2023 Warner Losh <imp@bsdimp.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * OS-Specific portion of syscall_defs.h
 */

#include "netbsd/syscall_nr.h"

/*
 * time_t seems to be very inconsistly defined for the different *BSD's...
 *
 * NetBSD always uses int64_t.
 */
typedef int64_t target_time_t;
