#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "exec/cpu-common.h"
#include "sysemu/xen-mapcache.h"

#ifdef CONFIG_XEN

void xen_invalidate_map_cache_entry(uint8_t *buffer)
{
}

uint8_t *xen_map_cache(hwaddr phys_addr, hwaddr size, uint8_t lock, bool dma)
{
    return NULL;
}

ram_addr_t xen_ram_addr_from_mapcache(void *ptr)
{
    return 0;
}

#endif
