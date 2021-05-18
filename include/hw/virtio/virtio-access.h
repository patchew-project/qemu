/*
 * Virtio Accessor Support: In case your target can change endian.
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Rusty Russell   <rusty@au.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef QEMU_VIRTIO_ACCESS_H
#define QEMU_VIRTIO_ACCESS_H

#include "exec/hwaddr.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"

#if defined(TARGET_PPC64) || defined(TARGET_ARM)
#define LEGACY_VIRTIO_IS_BIENDIAN 1
#endif

static inline bool virtio_access_is_big_endian(VirtIODevice *vdev)
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

#define VIRTIO_LD_CONVERT(size, rtype)\
static inline rtype virtio_ld ## size ## _phys(VirtIODevice *vdev, hwaddr pa)\
{\
    AddressSpace *dma_as = vdev->dma_as;\
\
    if (virtio_access_is_big_endian(vdev)) {\
        return ld ## size ## _be_phys(dma_as, pa);\
    }\
    return ld ## size ## _le_phys(dma_as, pa);\
}\
static inline rtype virtio_ld ## size ## _p(VirtIODevice *vdev,\
                                            const void *ptr)\
{\
    if (virtio_access_is_big_endian(vdev)) {\
        return ld ## size ## _be_p(ptr);\
    } else {\
        return ld ## size ## _le_p(ptr);\
    }\
}\
static inline rtype virtio_ld ## size ## _phys_cached(VirtIODevice *vdev,\
                                                      MemoryRegionCache *cache,\
                                                      hwaddr pa)\
{\
    if (virtio_access_is_big_endian(vdev)) {\
        return ld ## size ## _be_phys_cached(cache, pa);\
    }\
    return ld ## size ## _le_phys_cached(cache, pa);\
}

#define VIRTIO_ST_CONVERT(size, vtype)\
static inline void virtio_st## size ## _p(VirtIODevice *vdev,\
                                          void *ptr, vtype v)\
{\
    if (virtio_access_is_big_endian(vdev)) {\
        st## size ## _be_p(ptr, v);\
    } else {\
        st## size ## _le_p(ptr, v);\
    }\
}\
static inline void virtio_st## size ## _phys(VirtIODevice *vdev,\
                                             hwaddr pa, vtype value)\
{\
    AddressSpace *dma_as = vdev->dma_as;\
\
    if (virtio_access_is_big_endian(vdev)) {\
        st## size ## _be_phys(dma_as, pa, value);\
    } else {\
        st## size ## _le_phys(dma_as, pa, value);\
    }\
}\
static inline void virtio_st ## size ## _phys_cached(VirtIODevice *vdev,\
                                                     MemoryRegionCache *cache,\
                                                     hwaddr pa, vtype value)\
{\
    if (virtio_access_is_big_endian(vdev)) {\
        st ## size ## _be_phys_cached(cache, pa, value);\
    } else {\
        st ## size ## _le_phys_cached(cache, pa, value);\
    }\
}

#define VIRTIO_LDST_CONVERT(size, rtype, vtype)\
    VIRTIO_LD_CONVERT(size, rtype)\
    VIRTIO_ST_CONVERT(size, vtype)

VIRTIO_LD_CONVERT(uw, uint16_t)
VIRTIO_ST_CONVERT(w, uint16_t)
VIRTIO_LDST_CONVERT(l, int, uint32_t)
VIRTIO_LDST_CONVERT(q, uint64_t, uint64_t)

static inline uint16_t virtio_tswap16(VirtIODevice *vdev, uint16_t s)
{
#ifdef HOST_WORDS_BIGENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap16(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap16(s) : s;
#endif
}

static inline void virtio_tswap16s(VirtIODevice *vdev, uint16_t *s)
{
    *s = virtio_tswap16(vdev, *s);
}

static inline uint32_t virtio_tswap32(VirtIODevice *vdev, uint32_t s)
{
#ifdef HOST_WORDS_BIGENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap32(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap32(s) : s;
#endif
}

static inline void virtio_tswap32s(VirtIODevice *vdev, uint32_t *s)
{
    *s = virtio_tswap32(vdev, *s);
}

static inline uint64_t virtio_tswap64(VirtIODevice *vdev, uint64_t s)
{
#ifdef HOST_WORDS_BIGENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap64(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap64(s) : s;
#endif
}

static inline void virtio_tswap64s(VirtIODevice *vdev, uint64_t *s)
{
    *s = virtio_tswap64(vdev, *s);
}
#endif /* QEMU_VIRTIO_ACCESS_H */
