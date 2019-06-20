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
 * @fw_cfg: fw_cfg device being modified
 *
 * Add a new named file containing the host crypto policy.
 *
 * This method is called by the machine_done() Notifier of
 * some implementations of MachineState, currently the X86
 * PCMachineState and the ARM VirtMachineState.
 */
void edk2_add_host_crypto_policy(FWCfgState *fw_cfg);

#endif /* HW_FIRMWARE_UEFI_EDK2_H */
