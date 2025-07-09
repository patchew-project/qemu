/*
 * QEMU target info API (returning QAPI types)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_EXTRA_H
#define QEMU_TARGET_INFO_EXTRA_H

#include "qapi/qapi-types-machine.h"

/**
 * target_arch:
 *
 * Returns: QAPI SysEmuTarget enum (i.e. SYS_EMU_TARGET_X86_64).
 */
SysEmuTarget target_arch(void);

/**
 * target_base_arch:
 *
 * Returns: QAPI SysEmuTarget enum (i.e. SYS_EMU_TARGET_I386).
 */
SysEmuTarget target_base_arch(void);

#endif
