#ifndef LIBVHOST_ACCESS_H

#include "qemu/osdep.h"
#include "qemu/bswap.h"

#include "libvhost-user.h"

#if defined(TARGET_PPC64) || defined(TARGET_ARM)
#define LEGACY_VIRTIO_IS_BIENDIAN 1
#endif

static inline bool vu_is_big_endian(VuDev *dev)
{
    if (!vu_has_feature(dev, VIRTIO_F_VERSION_1)) {
        /* TODO there is no `device_endian` attribute for VuDev */
        /* assert(vdev->device_endian != VIRTIO_DEVICE_ENDIAN_UNKNOWN); */
        /* return vdev->device_endian == VIRTIO_DEVICE_ENDIAN_BIG; */
    }

    /* Devices conforming to VIRTIO 1.0 or later are always LE. */
    return false;
}

static inline bool vu_access_is_big_endian(VuDev *dev)
{
#if defined(LEGACY_VIRTIO_IS_BIENDIAN)
    return vu_is_big_endian(dev);
#elif defined(TARGET_WORDS_BIGENDIAN)
    if (vu_has_feature(dev, VIRTIO_F_VERSION_1)) {
        /* Devices conforming to VIRTIO 1.0 or later are always LE. */
        return false;
    }
    return true;
#else
    return false;
#endif
}

static inline void vu_stw_p(VuDev *vdev, void *ptr, uint16_t v)
{
    if (vu_access_is_big_endian(vdev)) {
        stw_be_p(ptr, v);
    } else {
        stw_le_p(ptr, v);
    }
}

static inline void vu_stl_p(VuDev *vdev, void *ptr, uint32_t v)
{
    if (vu_access_is_big_endian(vdev)) {
        stl_be_p(ptr, v);
    } else {
        stl_le_p(ptr, v);
    }
}

static inline void vu_stq_p(VuDev *vdev, void *ptr, uint64_t v)
{
    if (vu_access_is_big_endian(vdev)) {
        stq_be_p(ptr, v);
    } else {
        stq_le_p(ptr, v);
    }
}

static inline int vu_lduw_p(VuDev *vdev, const void *ptr)
{
    if (vu_access_is_big_endian(vdev))
        return lduw_be_p(ptr);
    return lduw_le_p(ptr);
}

static inline int vu_ldl_p(VuDev *vdev, const void *ptr)
{
    if (vu_access_is_big_endian(vdev))
        return ldl_be_p(ptr);
    return ldl_le_p(ptr);
}

static inline uint64_t vu_ldq_p(VuDev *vdev, const void *ptr)
{
    if (vu_access_is_big_endian(vdev))
        return ldq_be_p(ptr);
    return ldq_le_p(ptr);
}

#endif /* LIBVHOST_ACCESS_h */
