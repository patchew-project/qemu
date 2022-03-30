#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/block-backend.h"

BlockBackend *blk_by_qdev_id(const char *id, Error **errp)
{
    error_setg(errp, "blk '%s' not found", id);
    return NULL;
}
