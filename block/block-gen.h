#ifndef BLOCK_BLOCK_GEN_H
#define BLOCK_BLOCK_GEN_H

#include "block/block_int.h"

/* This function is called at the end of generated coroutine entries. */
static inline void bdrv_poll_co__on_exit(void)
{
    aio_wait_kick();
}

/* Base structure for argument packing structures */
typedef struct BdrvPollCo {
    BlockDriverState *bs;
    bool in_progress;
    int ret;
    Coroutine *co; /* Keep pointer here for debugging */
} BdrvPollCo;

static inline int bdrv_poll_co(BdrvPollCo *s)
{
    assert(!qemu_in_coroutine());

    bdrv_coroutine_enter(s->bs, s->co);
    BDRV_POLL_WHILE(s->bs, s->in_progress);

    return s->ret;
}

#endif /* BLOCK_BLOCK_GEN_H */
