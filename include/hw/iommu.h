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
    /* hardware nested stage-1 page table support */
    VIOMMU_CAP_HW_NESTED = BIT_ULL(0),
};

#endif /* HW_IOMMU_H */
