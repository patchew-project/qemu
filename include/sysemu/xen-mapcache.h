/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef XEN_MAPCACHE_H
#define XEN_MAPCACHE_H

typedef hwaddr (*phys_offset_to_gaddr_t)(hwaddr start_addr,
                                                     ram_addr_t size,
                                                     void *opaque);
/* FIXME ARM supported since Xen 4.3? */
#if defined(CONFIG_XEN) /* XXX supported_xen_target() wrong? */ && \
    !defined(HOST_ARM) && !defined(HOST_AARCH64)

void xen_map_cache_init(phys_offset_to_gaddr_t f,
                        void *opaque);
uint8_t *xen_map_cache(hwaddr phys_addr, hwaddr size,
                       uint8_t lock, bool dma);
ram_addr_t xen_ram_addr_from_mapcache(void *ptr);
void xen_invalidate_map_cache_entry(uint8_t *buffer);
void xen_invalidate_map_cache(void);

#else

static inline void xen_map_cache_init(phys_offset_to_gaddr_t f,
                                      void *opaque)
{
}

static inline uint8_t *xen_map_cache(hwaddr phys_addr,
                                     hwaddr size,
                                     uint8_t lock,
                                     bool dma)
{
    abort();
}

static inline ram_addr_t xen_ram_addr_from_mapcache(void *ptr)
{
    abort();
}

static inline void xen_invalidate_map_cache_entry(uint8_t *buffer)
{
}

static inline void xen_invalidate_map_cache(void)
{
}

#endif

#endif /* XEN_MAPCACHE_H */
