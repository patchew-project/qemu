#include "qemu/osdep.h"
#include "qmp-commands.h"

GuidInfo *qmp_query_vm_generation_id(Error **errp)
{
    error_setg(errp, "this command is not currently supported");
    return NULL;
}
