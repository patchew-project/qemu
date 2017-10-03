#include "qemu/osdep.h"
#include "qmp-commands.h"
#include "qapi/qmp/qerror.h"

VirtioInfo *qmp_query_virtio(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
