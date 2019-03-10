/*
 * UEFI EDK2 Support
 *
 * Copyright (c) 2019 Red Hat Inc.
 *
 * Author:
 *  Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_FIRMWARE_UEFI_EDK2_H
#define HW_FIRMWARE_UEFI_EDK2_H

#include "hw/nvram/fw_cfg.h"

/**
 * edk2_add_host_crypto_policy:
 * @s: fw_cfg device being modified
 *
 * Add a new named file containing the host crypto policy.
 *
 * Currently only the 'https' policy is supported.
 */
void edk2_add_host_crypto_policy(FWCfgState *s);

#endif /* HW_FIRMWARE_UEFI_EDK2_H */
