/*
 * Legacy virtio endian helpers.
 *
 * Copyright Red Hat, Inc. 2020
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-access.h"

#if defined(TARGET_PPC64) || defined(TARGET_ARM)
# define LEGACY_VIRTIO_IS_BIENDIAN 1
#endif

bool virtio_access_is_big_endian(VirtIODevice *vdev)
{
#if defined(LEGACY_VIRTIO_IS_BIENDIAN)
    return virtio_is_big_endian(vdev);
#elif defined(TARGET_WORDS_BIGENDIAN)
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        /* Devices conforming to VIRTIO 1.0 or later are always LE. */
        return false;
    }
    return true;
#else
    return false;
#endif
}
