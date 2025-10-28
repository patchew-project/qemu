/*
 * Hardened and lockless Single Producer Single Consumer Queue implemented
 * over shared-memory.
 *
 * The queue implementation does not look at packet contents, it's up to upper
 * layers to make sure data is produced and parsed safely. All data is copied
 * in/out from/to local private buffers so the peer cannot mess with them while
 * upper layers parse.
 *
 * The queue is split into a private and a shared part.
 * The private part contains cached and sanitized versions of the indexes that
 * indicate our position in the ring-buffer. Peers can corrupt the shared area
 * but have no access to the private area. So whenever we copy from the shared
 * area into the private one, we need to sanitize indexes and make sure they
 * are within bounds.
 *
 * A malicious peer can send corrupt data, it can stop receiving or flood the
 * queue causing a sort of denial of service but it can NOT cause our side
 * to copy data in or out of buffers outside of the shared memory area.
 *
 * This implementation expects the SHM area to be cache-coherent or uncached.
 * The shared area can be mapped in different ways and our peer may be anything
 * from another thread on our same OS to an FPGA implementation on a PCI card.
 * So local CPU cache-lines sizes, or spin-locks and things that work on a
 * single CPU cluster are not used. Instead the implementation sticks to atomic
 * load/stores of 32b values and to using memory-barriers to guarantee ordering.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SPSC_QUEUE_H__
#define SPSC_QUEUE_H__

#include <assert.h>
#include "qemu/atomic.h"

#define BUG_ON(x) assert(!(x))

#define SPSC_QUEUE_MAX_PACKET_SIZE 64
/*
 * This cache-line size is used to align fields in the hope of
 * avoiding cache-line ping-pong:ing. Since the queue layout is
 * used across heterogenous CPU clusters and across FPGA/HW implementations,
 * a fixed size must be used, i.e not the local CPU's cache-line size.
 */
#define SPSC_QUEUE_CACHE_LINE_SIZE 64

typedef struct spsc_queue_shared {
    uint32_t head __attribute__((__aligned__(SPSC_QUEUE_CACHE_LINE_SIZE)));
    uint32_t tail __attribute__((__aligned__(SPSC_QUEUE_CACHE_LINE_SIZE)));
    uint32_t packets[][SPSC_QUEUE_MAX_PACKET_SIZE / 4]
        __attribute__((__aligned__(SPSC_QUEUE_CACHE_LINE_SIZE)));
} spsc_queue_shared;

typedef struct spsc_queue {
    uint32_t cached_tail;
    uint32_t cached_head;
    spsc_queue_shared *shm;
    const char *name;
    unsigned int capacity;
} spsc_queue;

/* Atomically load and sanitize an index from the SHM area.  */
static inline uint32_t spsc_atomic_load(spsc_queue *q, uint32_t *ptr)
{
    uint32_t val;

    val = qatomic_read(ptr);
    /* Make sure packet reads are done after reading the index.  */
    smp_mb_acquire();

    /* Bounds check that index is within queue size.  */
    if (val >= q->capacity) {
        val = val % q->capacity;
    }

    return val;
}

static inline void spsc_atomic_store(spsc_queue *q, uint32_t *ptr, uint32_t v)
{
    /* Make sure packet-data gets written before updating the index.  */
    smp_mb_release();
    qatomic_set(ptr, v);
}

/* Returns the capacity of a queue given a specific mapsize. */
static inline unsigned int spsc_capacity(size_t mapsize)
{
    unsigned int capacity;
    spsc_queue *q = NULL;

    if (mapsize < sizeof(*q->shm)) {
        return 0;
    }

    /* Start with the size of the shared area. */
    mapsize -= sizeof(*q->shm);
    capacity = mapsize / sizeof(q->shm->packets[0]);

    if (capacity < 2) {
        /* Capacities of less than 2 are invalid. */
        return 0;
    }

    return capacity;
}

static inline size_t spsc_mapsize(unsigned int capacity)
{
    spsc_queue *q = NULL;
    size_t mapsize;

    BUG_ON(capacity < 2);

    mapsize = sizeof(*q->shm);
    mapsize += sizeof(q->shm->packets[0]) * capacity;

    return mapsize;
}

static inline void spsc_init(spsc_queue *q, const char *name, size_t capacity,
                             void *mem)
{
    BUG_ON(!mem);

    /* Initialize private queue area to all zeroes */
    memset(q, 0, sizeof *q);

    q->shm = (spsc_queue_shared *) mem;
    q->name = name;
    q->capacity = capacity;

    /* In case we're opening a pre-existing queue, pick up where we left off. */
    q->cached_tail = spsc_atomic_load(q, &q->shm->tail);
    q->cached_head = spsc_atomic_load(q, &q->shm->head);
}

static inline bool spsc_queue_is_full(spsc_queue *q)
{
    uint32_t next_head;
    uint32_t head;

    head = spsc_atomic_load(q, &q->shm->head);

    next_head = head + 1;
    if (next_head >= q->capacity) {
        next_head = 0;
    }

    if (next_head == q->cached_tail) {
        q->cached_tail = spsc_atomic_load(q, &q->shm->tail);
        if (next_head == q->cached_tail) {
            return true;
        }
    }
    return false;
}

static inline bool spsc_send(spsc_queue *q, void *buf, size_t size)
{
    uint32_t next_head;
    uint32_t head;

    BUG_ON(size > sizeof q->shm->packets[0]);
    BUG_ON(size == 0);

    /* Is the queue full?  */
    if (spsc_queue_is_full(q)) {
        return false;
    }

    head = spsc_atomic_load(q, &q->shm->head);
    next_head = head + 1;
    if (next_head >= q->capacity) {
        next_head = 0;
    }

    memcpy(q->shm->packets[head], buf, size);

    spsc_atomic_store(q, &q->shm->head, next_head);
    return true;
}

static inline bool spsc_recv(spsc_queue *q, void *buf, size_t size)
{
    uint32_t tail;

    BUG_ON(size > sizeof q->shm->packets[0]);
    BUG_ON(size == 0);

    tail = spsc_atomic_load(q, &q->shm->tail);

    /* Is the queue empty?  */
    if (tail == q->cached_head) {
        q->cached_head = spsc_atomic_load(q, &q->shm->head);
        if (tail == q->cached_head) {
            return false;
        }
    }

    memcpy(buf, q->shm->packets[tail], size);

    /* Update the read pointer.  */
    tail++;
    if (tail >= q->capacity) {
        tail = 0;
    }

    spsc_atomic_store(q, &q->shm->tail, tail);
    return true;
}
#endif /* SPSC_QUEUE_H__ */
