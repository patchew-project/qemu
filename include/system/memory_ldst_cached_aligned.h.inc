/*
 *  Aligned Memory access templates for MemoryRegionCache
 *
 *  Callers MUST ensure @addr is aligned to the access size before invoking
 *  these helpers; otherwise the behavior is undefined.
 *
 *  Copyright (c) 2018 Red Hat, Inc.
 *  Copyright (c) 2018 LanxinComputing, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define ADDRESS_SPACE_LD_CACHED_ALIGNED(size) \
    glue(glue(address_space_ld, size), glue(ENDIANNESS, _cached_aligned))
#define ADDRESS_SPACE_LD_CACHED_SLOW(size) \
    glue(glue(address_space_ld, size), glue(ENDIANNESS, _cached_slow))
#define LD_P_ALIGNED(size) \
    glue(glue(ld, size), glue(ENDIANNESS, _p_aligned))
#define LD_PHYS_CACHED_ALIGNED(size) \
    glue(glue(ld, size), glue(ENDIANNESS, glue(_phys, _cached_aligned)))

static inline uint16_t ADDRESS_SPACE_LD_CACHED_ALIGNED(uw)(MemoryRegionCache *cache,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len && 2 <= cache->len - addr);
    fuzz_dma_read_cb(cache->xlat + addr, 2, cache->mrs.mr);
    if (likely(cache->ptr)) {
        return LD_P_ALIGNED(uw)(cache->ptr + addr);
    } else {
        return ADDRESS_SPACE_LD_CACHED_SLOW(uw)(cache, addr, attrs, result);
    }
}

static inline uint16_t LD_PHYS_CACHED_ALIGNED(uw)(MemoryRegionCache *cache,
                                                  hwaddr addr)
{
    return ADDRESS_SPACE_LD_CACHED_ALIGNED(uw)(cache, addr,
                                              MEMTXATTRS_UNSPECIFIED, NULL);
}

#undef ADDRESS_SPACE_LD_CACHED_ALIGNED
#undef ADDRESS_SPACE_LD_CACHED_SLOW
#undef LD_P_ALIGNED
#undef LD_PHYS_CACHED_ALIGNED

#define ADDRESS_SPACE_ST_CACHED_ALIGNED(size) \
    glue(glue(address_space_st, size), glue(ENDIANNESS, _cached_aligned))
#define ADDRESS_SPACE_ST_CACHED_SLOW(size) \
    glue(glue(address_space_st, size), glue(ENDIANNESS, _cached_slow))
#define ST_P_ALIGNED(size) \
    glue(glue(st, size), glue(ENDIANNESS, _p_aligned))
#define ST_PHYS_CACHED_ALIGNED(size) \
    glue(glue(st, size), glue(ENDIANNESS, glue(_phys, _cached_aligned)))

static inline void ADDRESS_SPACE_ST_CACHED_ALIGNED(w)(const MemoryRegionCache *cache,
    hwaddr addr, uint16_t val, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len && 2 <= cache->len - addr);
    if (likely(cache->ptr)) {
        ST_P_ALIGNED(w)(cache->ptr + addr, val);
    } else {
        ADDRESS_SPACE_ST_CACHED_SLOW(w)(cache, addr, val, attrs, result);
    }
}

static inline void ST_PHYS_CACHED_ALIGNED(w)(MemoryRegionCache *cache,
    hwaddr addr, uint16_t val)
{
    ADDRESS_SPACE_ST_CACHED_ALIGNED(w)(cache, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
}

#undef ADDRESS_SPACE_ST_CACHED_ALIGNED
#undef ADDRESS_SPACE_ST_CACHED_SLOW
#undef ST_P_ALIGNED
#undef ST_PHYS_CACHED_ALIGNED

#undef ENDIANNESS
