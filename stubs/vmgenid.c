#include "qemu/osdep.h"
#include "qmp-commands.h"

GuidInfo *qmp_query_vm_generation_id(Error **errp)
{
    error_setg(errp, "this command is not currently supported");
    return NULL;
}

void qmp_set_vm_generation_id(bool changed, const char *guid, Error **errp)
{
    error_setg(errp, "this command is not currently supported");
    return;
}
