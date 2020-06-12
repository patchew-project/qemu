#include "qemu/osdep.h"
#include "fd-trans.h"

struct fd_trans_table *fd_trans_table_clone(struct fd_trans_table *tbl)
{
    struct fd_trans_table *new_tbl = g_new0(struct fd_trans_table, 1);
    new_tbl->fd_max = tbl->fd_max;
    new_tbl->entries = g_new0(TargetFdTrans*, tbl->fd_max);
    memcpy(new_tbl->entries,
           tbl->entries,
           sizeof(*new_tbl->entries) * tbl->fd_max);
    return new_tbl;
}
