/*
 * QEMU IGVM configuration backend for Confidential Guests
 *
 * Copyright (C) 2023-2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@suse.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC_IGVM_H
#define EXEC_IGVM_H

#include "exec/confidential-guest-support.h"

#if defined(CONFIG_IGVM)

void igvm_file_init(ConfidentialGuestSupport *cgs);
void igvm_process(ConfidentialGuestSupport *cgs);

#else

static inline void igvm_file_init(ConfidentialGuestSupport *cgs)
{
}

static inline void igvm_process(ConfidentialGuestSupport *cgs)
{
}

#endif

#endif
