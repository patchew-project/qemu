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

static inline uint16_t virtio_lduw_phys(VirtIODevice *vdev, hwaddr pa)
{
    AddressSpace *dma_as = vdev->dma_as;

    if (virtio_access_is_big_endian(vdev)) {
        return lduw_be_phys(dma_as, pa);
    }
    return lduw_le_phys(dma_as, pa);
}

static inline uint32_t virtio_ldl_phys(VirtIODevice *vdev, hwaddr pa)
{
    AddressSpace *dma_as = vdev->dma_as;

    if (virtio_access_is_big_endian(vdev)) {
        return ldl_be_phys(dma_as, pa);
    }
    return ldl_le_phys(dma_as, pa);
}

static inline uint64_t virtio_ldq_phys(VirtIODevice *vdev, hwaddr pa)
{
    AddressSpace *dma_as = vdev->dma_as;

    if (virtio_access_is_big_endian(vdev)) {
        return ldq_be_phys(dma_as, pa);
    }
    return ldq_le_phys(dma_as, pa);
}

static inline void virtio_stw_phys(VirtIODevice *vdev, hwaddr pa,
                                   uint16_t value)
{
    AddressSpace *dma_as = vdev->dma_as;

    if (virtio_access_is_big_endian(vdev)) {
        stw_be_phys(dma_as, pa, value);
    } else {
        stw_le_phys(dma_as, pa, value);
    }
}

static inline void virtio_stl_phys(VirtIODevice *vdev, hwaddr pa,
                                   uint32_t value)
{
    AddressSpace *dma_as = vdev->dma_as;

    if (virtio_access_is_big_endian(vdev)) {
        stl_be_phys(dma_as, pa, value);
    } else {
        stl_le_phys(dma_as, pa, value);
    }
}

static inline void virtio_stw_p(VirtIODevice *vdev, void *ptr, uint16_t v)
{
    if (virtio_access_is_big_endian(vdev)) {
        stw_be_p(ptr, v);
    } else {
        stw_le_p(ptr, v);
    }
}

static inline void virtio_stl_p(VirtIODevice *vdev, void *ptr, uint32_t v)
{
    if (virtio_access_is_big_endian(vdev)) {
        stl_be_p(ptr, v);
    } else {
        stl_le_p(ptr, v);
    }
}

static inline void virtio_stq_p(VirtIODevice *vdev, void *ptr, uint64_t v)
{
    if (virtio_access_is_big_endian(vdev)) {
        stq_be_p(ptr, v);
    } else {
        stq_le_p(ptr, v);
    }
}

static inline uint16_t virtio_lduw_p(VirtIODevice *vdev, const void *ptr)
{
    if (virtio_access_is_big_endian(vdev)) {
        return lduw_be_p(ptr);
    } else {
        return lduw_le_p(ptr);
    }
}

static inline uint32_t virtio_ldl_p(VirtIODevice *vdev, const void *ptr)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldl_be_p(ptr);
    } else {
        return ldl_le_p(ptr);
    }
}

static inline uint64_t virtio_ldq_p(VirtIODevice *vdev, const void *ptr)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldq_be_p(ptr);
    } else {
        return ldq_le_p(ptr);
    }
}

static inline uint16_t virtio_tswap16(VirtIODevice *vdev, uint16_t s)
{
#ifdef HOST_WORDS_BIGENDIAN
    return virtio_access_is_big_endian(vdev) ? s : bswap16(s);
#else
    return virtio_access_is_big_endian(vdev) ? bswap16(s) : s;
#endif
}

/**
 * virtio_ld*_phys_cached_with_attrs: load from a VirtIO cached #MemoryRegion
 * virtio_st*_phys_cached_with_attrs: store to a VirtIO cached #MemoryRegion
 *
 * These functions perform a load or store of the byte, word,
 * longword or quad to the specified address.  The address is
 * a physical address in the VirtIO device AddressSpace, but it must lie within
 * a #MemoryRegion that was mapped with address_space_cache_init.
 *
 * @vdev: virtio device accessed
 * @cache: previously initialized #MemoryRegionCache to be accessed
 * @pa: physical address within the address space
 * @val: data value, for stores
 * @attrs: memory transaction attributes
 * @result: location to write the success/failure of the transaction;
 *   if NULL, this information is discarded
 */

static inline uint16_t virtio_lduw_phys_cached_with_attrs(VirtIODevice *vdev,
                                                MemoryRegionCache *cache,
                                                hwaddr pa, MemTxAttrs attrs,
                                                MemTxResult *result)
{
    if (virtio_access_is_big_endian(vdev)) {
        return address_space_lduw_be_cached(cache, pa, attrs, result);
    }
    return address_space_lduw_le_cached(cache, pa, attrs, result);
}

static inline uint16_t virtio_lduw_phys_cached(VirtIODevice *vdev,
                                               MemoryRegionCache *cache,
                                               hwaddr pa)
{
    return virtio_lduw_phys_cached_with_attrs(vdev, cache, pa,
                                              MEMTXATTRS_UNSPECIFIED, NULL);
}

static inline uint32_t virtio_ldl_phys_cached(VirtIODevice *vdev,
                                              MemoryRegionCache *cache,
                                              hwaddr pa)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldl_be_phys_cached(cache, pa);
    }
    return ldl_le_phys_cached(cache, pa);
}

static inline uint64_t virtio_ldq_phys_cached(VirtIODevice *vdev,
                                              MemoryRegionCache *cache,
                                              hwaddr pa)
{
    if (virtio_access_is_big_endian(vdev)) {
        return ldq_be_phys_cached(cache, pa);
    }
    return ldq_le_phys_cached(cache, pa);
}

static inline void virtio_stw_phys_cached(VirtIODevice *vdev,
                                          MemoryRegionCache *cache,
                                          hwaddr pa, uint16_t value)
{
    if (virtio_access_is_big_endian(vdev)) {
        stw_be_phys_cached(cache, pa, value);
    } else {
        stw_le_phys_cached(cache, pa, value);
    }
}

static inline void virtio_stl_phys_cached(VirtIODevice *vdev,
                                          MemoryRegionCache *cache,
                                          hwaddr pa, uint32_t value)
{
    if (virtio_access_is_big_endian(vdev)) {
        stl_be_phys_cached(cache, pa, value);
    } else {
        stl_le_phys_cached(cache, pa, value);
    }
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
