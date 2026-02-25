/*
 * Stubs for accelerated SMMU instance backed by an iommufd vIOMMU object.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/arm/smmuv3.h"
#include "hw/arm/smmuv3-accel.h"

void smmuv3_accel_init(SMMUv3State *s)
{
}

bool smmuv3_accel_install_ste(SMMUv3State *s, SMMUDevice *sdev, int sid,
                              Error **errp)
{
    return true;
}

bool smmuv3_accel_install_ste_range(SMMUv3State *s, SMMUSIDRange *range,
                                    Error **errp)
{
    return true;
}

bool smmuv3_accel_attach_gbpa_hwpt(SMMUv3State *s, Error **errp)
{
    return true;
}

bool smmuv3_accel_issue_inv_cmd(SMMUv3State *s, void *cmd, SMMUDevice *sdev,
                                Error **errp)
{
    return true;
}

void smmuv3_accel_idr_override(SMMUv3State *s)
{
}

void smmuv3_accel_reset(SMMUv3State *s)
{
}
