/*
 * QEMU target info API
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_H
#define QEMU_TARGET_INFO_H

#include "qapi/qapi-types-machine.h"

/**
 * target_name:
 *
 * Returns: Canonical target name (i.e. "i386").
 */
const char *target_name(void);

/**
 * target_system_arch:
 *
 * Returns: QAPI SysEmuTarget enum (i.e. SYS_EMU_TARGET_I386).
 */
SysEmuTarget target_system_arch(void);

/**
 * target_long_bits:
 *
 * Returns: number of bits in a long type for this target (i.e. 64).
 */
unsigned target_long_bits(void);

/**
 * target_machine_typename:
 *
 * Returns: Name of the QOM interface implemented by machines
 *          usable on this target binary.
 */
const char *target_machine_typename(void);

/**
 * target_cpu_type:
 *
 * Returns: target CPU base QOM type name (i.e. TYPE_X86_CPU).
 */
const char *target_cpu_type(void);

#endif
