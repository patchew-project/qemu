/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 *
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_PCI_FIXED_BAR_VALIDATE_H
#define HW_PCI_FIXED_BAR_VALIDATE_H

typedef struct {
    bool      any_fixed;
    uint64_t  mmio32_base;
    uint64_t  mmio32_limit;
    uint64_t  mmio64_base;
    uint64_t  mmio64_limit;
    GArray    *claims;
} FixedBarsInfo;

bool fixed_bars_validate(FixedBarsInfo *info, Error **errp);

#endif /* HW_PCI_FIXED_BAR_VALIDATE_H */
