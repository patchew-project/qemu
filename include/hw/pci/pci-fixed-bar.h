/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 * Check the fixed BAR configuration of a machine.
 *
 * Written by Tushar Dave
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_FIXED_BAR_H
#define HW_PCI_FIXED_BAR_H

#include "qapi/error.h"

/**
 * fixed_bars_check: validate the fixed-bar-N configuration of all devices
 * @mmio32_base, @mmio32_size: the 32-bit PCIe MMIO aperture
 * @mmio64_base, @mmio64_size: the 64-bit PCIe MMIO aperture, size 0 if absent
 * @errp: set on a validation failure
 *
 * Returns true if at least one device has a fixed BAR and the configuration
 * is valid, false if no device has one or on failure (@errp is set).
 */
bool fixed_bars_check(uint64_t mmio32_base, uint64_t mmio32_size,
                      uint64_t mmio64_base, uint64_t mmio64_size,
                      Error **errp);

#endif /* HW_PCI_FIXED_BAR_H */
