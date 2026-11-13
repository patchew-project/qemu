/*
 * ARM SMC call dispatch
 *
 * Board and SoC models can register handlers for vendor or firmware
 * SMC calling convention function IDs. Under TCG, dispatch follows the
 * architectural trap checks and precedes exception delivery and QEMU's
 * PSCI emulation, so firmware services can share the SMC conduit with PSCI.
 *
 * Copyright (C) 2026 Process Mission
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_ARM_SMC_H
#define HW_ARM_ARM_SMC_H

typedef bool (*ARMSCMCHandler)(const uint64_t args[8], uint64_t *ret,
                              void *opaque);

/*
 * Register a handler for one SMCCC function ID.  The handler returns
 * true when it has handled the call and set *ret. args contains the
 * caller's x0-x7 or zero-extended r0-r7; the ARM dispatcher writes *ret
 * to x0/r0 and preserves the other registers. Returns 0 on success and
 * a negative value if the function ID is already registered.
 */
int arm_register_smc_handler(uint64_t func_id, ARMSCMCHandler handler,
                             void *opaque);
void arm_unregister_smc_handler(uint64_t func_id, ARMSCMCHandler handler);

#endif /* HW_ARM_ARM_SMC_H */
