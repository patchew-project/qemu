#include "qemu/osdep.h"
#include "qmp-commands.h"
#include "qapi/qmp/qerror.h"

VirtioInfoList *qmp_query_virtio(bool has_path, const char *path, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
