#include "qemu/osdep.h"
#include "system/block-backend.h"
#include "block/export.h"

BlockExport *blk_exp_find_by_blk(BlockBackend *blk)
{
    return NULL;
}

void warn_block_export_exists(const char *id)
{
    /* do nothing */
}
