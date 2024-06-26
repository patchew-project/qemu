#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/block-backend.h"

BlockBackend *blk_by_qdev_id(const char *id, Error **errp)
{
    /*
     * We expect this when blockdev-change is called with parent-type=qdev,
     * but qdev-monitor is not linked in. So no blk_ API is not available.
     */
    error_setg(errp, "Parameter 'parent-type' does not accept value 'qdev'");
    return NULL;
}
