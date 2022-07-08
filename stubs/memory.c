#include "qemu/osdep.h"
#include "exec/memory.h"

MemoryRegion *memory_region_from_host(void *host, ram_addr_t *offset)
{
    return NULL;
}

int memory_region_get_fd(MemoryRegion *mr)
{
    return -1;
}

