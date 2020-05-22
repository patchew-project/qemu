#ifndef BLOCK_COROUTINES_INT_H
#define BLOCK_COROUTINES_INT_H

#include "block/block_int.h"

int coroutine_fn bdrv_co_check(BlockDriverState *bs,
                               BdrvCheckResult *res, BdrvCheckMode fix);
void coroutine_fn bdrv_co_invalidate_cache(BlockDriverState *bs, Error **errp);

int coroutine_fn
bdrv_co_prwv(BdrvChild *child, int64_t offset, QEMUIOVector *qiov,
             bool is_write, BdrvRequestFlags flags);
int
bdrv_prwv(BdrvChild *child, int64_t offset, QEMUIOVector *qiov,
          bool is_write, BdrvRequestFlags flags);

int coroutine_fn
bdrv_co_common_block_status_above(BlockDriverState *bs,
                                  BlockDriverState *base,
                                  bool want_zero,
                                  int64_t offset,
                                  int64_t bytes,
                                  int64_t *pnum,
                                  int64_t *map,
                                  BlockDriverState **file);
int
bdrv_common_block_status_above(BlockDriverState *bs,
                               BlockDriverState *base,
                               bool want_zero,
                               int64_t offset,
                               int64_t bytes,
                               int64_t *pnum,
                               int64_t *map,
                               BlockDriverState **file);

int coroutine_fn
bdrv_co_rw_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos,
                   bool is_read);
int
bdrv_rw_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos,
                bool is_read);

#endif /* BLOCK_COROUTINES_INT_H */
