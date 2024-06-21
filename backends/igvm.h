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

#ifndef BACKENDS_IGVM_H
#define BACKENDS_IGVM_H

#include "exec/confidential-guest-support.h"
#include "sysemu/igvm-cfg.h"
#include "qapi/error.h"

#if defined(CONFIG_IGVM)

int igvm_process_file(IgvmCfgState *igvm, ConfidentialGuestSupport *cgs,
                      Error **errp);

#else

static inline int igvm_process_file(IgvmCfgState *igvm,
                                    ConfidentialGuestSupport *cgs, Error **errp)
{
    error_setg(
        errp, "Invalid call to igvm_process_file when CONFIG_IGVM is disabled");
    return -1;
}

#endif

#endif
