
#include "qemu/osdep.h"
#include "sysemu/hostmem.h"

void host_memory_backend_set_mapped(HostMemoryBackend *backend, bool mapped)
{
}


MemoryRegion *host_memory_backend_get_memory(HostMemoryBackend *backend,
                                             Error **errp)
{
    return NULL;
}
