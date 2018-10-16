/*
 *    Definitions for the 'struct ptr_ring' datastructure.
 *
 *    Author:
 *        Michael S. Tsirkin <mst@redhat.com>
 *        Xiao Guangrong <xiaoguangrong@tencent.com>
 *
 *    Copyright (C) 2018 Red Hat, Inc.
 *    Copyright (C) 2018 Tencent, Inc.
 *
 *    This program is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by the
 *    Free Software Foundation; either version 2 of the License, or (at your
 *    option) any later version.
 *
 *    This is a limited-size FIFO maintaining pointers in FIFO order, with
 *    one CPU producing entries and another consuming entries from a FIFO.
 *
 *    This implementation tries to minimize cache-contention when there is a
 *    single producer and a single consumer CPU.
 */

#ifndef _QEMU_PTR_RING_H
#define _QEMU_PTR_RING_H 1

#include "qemu/compiler.h"
#include "qemu/atomic.h"

#define SMP_CACHE_BYTES      64
#define ____cacheline_aligned_in_smp \
        __attribute__((__aligned__(SMP_CACHE_BYTES)))

#define WRITE_ONCE(ptr, val) \
    (*((volatile typeof(ptr) *)(&(ptr))) = (val))
#define READ_ONCE(ptr) (*((volatile typeof(ptr) *)(&(ptr))))

struct ptr_ring {
    int producer ____cacheline_aligned_in_smp;
    int consumer_head ____cacheline_aligned_in_smp; /* next valid entry */
    int consumer_tail; /* next entry to invalidate */
    /* Shared consumer/producer data */
    /* Read-only by both the producer and the consumer */
    int size ____cacheline_aligned_in_smp; /* max entries in queue */
    int batch; /* number of entries to consume in a batch */
    void **queue;
};
typedef struct ptr_ring Ptr_Ring;

/*
 * Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().
 */
static inline bool ptr_ring_full(struct ptr_ring *r)
{
    return r->queue[r->producer];
}

/*
 * Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().
 *
 * Callers are responsible for making sure pointer that is being queued
 * points to a valid data.
 */
static inline int ptr_ring_produce(struct ptr_ring *r, void *ptr)
{
    if (unlikely(!r->size) || r->queue[r->producer])
        return -ENOSPC;

    /* Make sure the pointer we are storing points to a valid data. */
    /* Pairs with READ_ONCE in ptr_ring_consume. */
    smp_wmb();

    WRITE_ONCE(r->queue[r->producer++], ptr);
    if (unlikely(r->producer >= r->size))
        r->producer = 0;
    return 0;
}

static inline void *__ptr_ring_peek(struct ptr_ring *r)
{
    if (likely(r->size))
        return READ_ONCE(r->queue[r->consumer_head]);
    return NULL;
}

/*
 * Test ring empty status.
 *
 * However, if some other CPU consumes ring entries at the same time,
 * the value returned is not guaranteed to be correct.
 *
 * In this case - to avoid incorrectly detecting the ring
 * as empty - the CPU consuming the ring entries is responsible
 * for either consuming all ring entries until the ring is empty,
 * or synchronizing with some other CPU and causing it to
 * re-test ptr_ring_empty and/or consume the ring enteries
 * after the synchronization point.
 *
 * Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().
 */
static inline bool ptr_ring_empty(struct ptr_ring *r)
{
    if (likely(r->size))
        return !r->queue[READ_ONCE(r->consumer_head)];
    return true;
}

/* Must only be called after __ptr_ring_peek returned !NULL */
static inline void __ptr_ring_discard_one(struct ptr_ring *r)
{
    /* Fundamentally, what we want to do is update consumer
     * index and zero out the entry so producer can reuse it.
     * Doing it naively at each consume would be as simple as:
     *       consumer = r->consumer;
     *       r->queue[consumer++] = NULL;
     *       if (unlikely(consumer >= r->size))
     *               consumer = 0;
     *       r->consumer = consumer;
     * but that is suboptimal when the ring is full as producer is writing
     * out new entries in the same cache line.  Defer these updates until a
     * batch of entries has been consumed.
     */
    /* Note: we must keep consumer_head valid at all times for ptr_ring_empty
     * to work correctly.
     */
    int consumer_head = r->consumer_head;
    int head = consumer_head++;

    /* Once we have processed enough entries invalidate them in
     * the ring all at once so producer can reuse their space in the ring.
     * We also do this when we reach end of the ring - not mandatory
     * but helps keep the implementation simple.
     */
    if (unlikely(consumer_head - r->consumer_tail >= r->batch ||
             consumer_head >= r->size)) {
        /* Zero out entries in the reverse order: this way we touch the
         * cache line that producer might currently be reading the last;
         * producer won't make progress and touch other cache lines
         * besides the first one until we write out all entries.
         */
        while (likely(head >= r->consumer_tail))
            r->queue[head--] = NULL;
        r->consumer_tail = consumer_head;
    }
    if (unlikely(consumer_head >= r->size)) {
        consumer_head = 0;
        r->consumer_tail = 0;
    }
    /* matching READ_ONCE in ptr_ring_empty for lockless tests */
    WRITE_ONCE(r->consumer_head, consumer_head);
}

static inline void *ptr_ring_consume(struct ptr_ring *r)
{
    void *ptr;

    /* The READ_ONCE in __ptr_ring_peek guarantees that anyone
     * accessing data through the pointer is up to date. Pairs
     * with smp_wmb in ptr_ring_produce.
     */
    ptr = __ptr_ring_peek(r);
    if (ptr)
        __ptr_ring_discard_one(r);

    return ptr;
}

static inline int ptr_ring_consume_batched(struct ptr_ring *r,
                         void **array, int n)
{
    void *ptr;
    int i;

    for (i = 0; i < n; i++) {
        ptr = ptr_ring_consume(r);
        if (!ptr)
            break;
        array[i] = ptr;
    }

    return i;
}

static inline void **__ptr_ring_init_queue_alloc(unsigned int size)
{
    return g_try_malloc0_n(size, sizeof(void *));
}

static inline void __ptr_ring_set_size(struct ptr_ring *r, int size)
{
    r->size = size;
    r->batch = SMP_CACHE_BYTES * 2 / sizeof(*(r->queue));
    /* We need to set batch at least to 1 to make logic
     * in __ptr_ring_discard_one work correctly.
     * Batching too much (because ring is small) would cause a lot of
     * burstiness. Needs tuning, for now disable batching.
     */
    if (r->batch > r->size / 2 || !r->batch)
        r->batch = 1;
}

/*
 * Disable batching so that there is no consumered entry in the ring.
 *
 * It is convenient if it makes sure that the ring is large enough to
 * contain all requests, i.e, ptr_ring_produce can not fail.
 */
static inline void ptr_ring_disable_batch(struct ptr_ring *r)
{
    r->batch = 1;
}

static inline int ptr_ring_init(struct ptr_ring *r, int size)
{
    r->queue = __ptr_ring_init_queue_alloc(size);
    if (!r->queue)
        return -ENOMEM;

    __ptr_ring_set_size(r, size);
    r->producer = r->consumer_head = r->consumer_tail = 0;
    return 0;
}

static inline void ptr_ring_cleanup(struct ptr_ring *r, void (*destroy)(void *))
{
    void *ptr;

    if (destroy)
        while ((ptr = ptr_ring_consume(r)))
            destroy(ptr);
    g_free(r->queue);
}
#endif
