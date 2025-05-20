/*
 * vfio-user specific definitions.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_USER_CONTAINER_H
#define HW_VFIO_USER_CONTAINER_H

#include <inttypes.h>
#include <stdbool.h>

#include "hw/vfio/vfio-container-base.h"
#include "hw/vfio-user/proxy.h"

/* MMU container sub-class for vfio-user. */
typedef struct VFIOUserContainer {
    VFIOContainerBase bcontainer;
    VFIOUserProxy *proxy;
} VFIOUserContainer;

OBJECT_DECLARE_SIMPLE_TYPE(VFIOUserContainer, VFIO_IOMMU_USER);

#endif /* HW_VFIO_USER_CONTAINER_H */
