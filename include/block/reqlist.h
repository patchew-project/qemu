/*
 * block_copy API
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2021 Virtuozzo International GmbH.
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef REQLIST_H
#define REQLIST_H

#include "qemu/coroutine.h"

/*
 * The API is not thread-safe and shouldn't be. The struct is public to be part
 * of other structures and protected by third-party locks, see
 * block/block-copy.c for example.
 */

typedef struct BlockReq {
    int64_t offset;
    int64_t bytes;

    CoQueue wait_queue; /* coroutines blocked on this req */
    QLIST_ENTRY(BlockReq) list;
} BlockReq;

typedef QLIST_HEAD(, BlockReq) BlockReqList;

void reqlist_init_req(BlockReqList *reqs, BlockReq *req, int64_t offset,
                      int64_t bytes);
BlockReq *reqlist_find_conflict(BlockReqList *reqs, int64_t offset,
                                int64_t bytes);
bool coroutine_fn reqlist_wait_one(BlockReqList *reqs, int64_t offset,
                                   int64_t bytes, CoMutex *lock);
void coroutine_fn reqlist_shrink_req(BlockReq *req, int64_t new_bytes);
void coroutine_fn reqlist_remove_req(BlockReq *req);

#endif /* REQLIST_H */
