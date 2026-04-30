/*
 * QEMU target info initialization
 *
 * Copyright (c) Qualcomm
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is included by each file defining a TargetInfo structure and is
 * responsible for registering it.
 */

#ifndef TARGET_INFO_DEF_H
#define TARGET_INFO_DEF_H

#define target_info_init(ti_var)        \
const TargetInfo *target_info(void)     \
{                                       \
    return &ti_var;                     \
}

#endif /* TARGET_INFO_DEF_H */
