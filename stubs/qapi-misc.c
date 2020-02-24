#include "qemu/osdep.h"
#include "qemu-common.h"

#include "qapi/qapi-commands-misc.h"
#include "./qapi/qapi-types-dump.h"
#include "qapi/qapi-commands-dump.h"

#pragma weak qmp_xen_load_devices_state

void qmp_dump_guest_memory(bool paging, const char *file,
                           bool has_detach, bool detach,
                           bool has_begin, int64_t begin, bool has_length,
                           int64_t length, bool has_format,
                           DumpGuestMemoryFormat format, Error **errp)
{
    qemu_debug_assert(0);
}

DumpQueryResult *qmp_query_dump(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

DumpGuestMemoryCapability *qmp_query_dump_guest_memory_capability(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

void qmp_xen_load_devices_state(const char *filename, Error **errp)
{
    qemu_debug_assert(0);
}

bool dump_in_progress(void)
{
    qemu_debug_assert(0);

    return FALSE;
}
