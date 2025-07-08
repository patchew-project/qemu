/*
 * General vIOMMU capabilities, flags, etc
 *
 * Copyright (C) 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IOMMU_H
#define HW_IOMMU_H

enum {
    VIOMMU_CAP_STAGE1 = BIT_ULL(0),  /* stage1 page table supported */
};

#endif /* HW_IOMMU_H */
