/*
 * Lockless Multithreads Implementation
 *
 * Implement lockless multithreads management.
 *
 * Note: currently only one producer is allowed.
 *
 * Copyright(C) 2018 Tencent Corporation.
 *
 * Author:
 *   Xiao Guangrong <xiaoguangrong@tencent.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/lockless-threads.h"

struct ThreadLocal {
    QemuThread thread;

    /* the event used to wake up the thread */
    QemuEvent ev;

    struct Threads *threads;

    /* local request ring which is filled by the user */
    Ptr_Ring request_ring;

    /* the index of the thread */
    int self;

    /* thread is useless and needs to exit */
    bool quit;
};
typedef struct ThreadLocal ThreadLocal;

/*
 * the main data struct represents multithreads which is shared by
 * all threads
 */
struct Threads {
    const char *name;
    unsigned int threads_nr;
    /* the request is pushed to the thread with round-robin manner */
    unsigned int current_thread_index;

    int thread_ring_size;
    int total_requests;

    /* the request is pre-allocated and linked in the list */
    int free_requests_nr;
    QSLIST_HEAD(, ThreadRequest) free_requests;

    /* the constructor of request */
    ThreadRequest *(*thread_request_init)(void);
    /* the destructor of request */
    void (*thread_request_uninit)(ThreadRequest *request);
    /* the handler of the request which is called in the thread */
    void (*thread_request_handler)(ThreadRequest *request);
    /*
     * the handler to process the result which is called in the
     * user's context
     */
    void (*thread_request_done)(ThreadRequest *request);

    /* the thread push the result to this ring so it has multiple producers */
    QemuSpin done_ring_lock;
    Ptr_Ring request_done_ring;

    ThreadLocal per_thread_data[0];
};
typedef struct Threads Threads;

static void put_done_request(Threads *threads, ThreadRequest *request)
{
    int ret;

    qemu_spin_lock(&threads->done_ring_lock);
    ret = ptr_ring_produce(&threads->request_done_ring, request);
    /* there should be enough room to save all request. */
    assert(!ret);
    qemu_spin_unlock(&threads->done_ring_lock);
}

/* retry to see if there is avilable request before actually go to wait. */
#define BUSY_WAIT_COUNT 1000

static ThreadRequest *thread_busy_wait_for_request(ThreadLocal *thread)
{
    ThreadRequest *request;
    int count = 0;

    for (count = 0; count < BUSY_WAIT_COUNT; count++) {
        request = ptr_ring_consume(&thread->request_ring);
        if (request) {
            return request;
        }

        cpu_relax();
    }

    return NULL;
}

static void *thread_run(void *opaque)
{
    ThreadLocal *self_data = (ThreadLocal *)opaque;
    Threads *threads = self_data->threads;
    void (*handler)(ThreadRequest *data) = threads->thread_request_handler;
    ThreadRequest *request;

    for ( ; !atomic_read(&self_data->quit); ) {
        qemu_event_reset(&self_data->ev);

        request = thread_busy_wait_for_request(self_data);
        if (!request) {
            qemu_event_wait(&self_data->ev);
            continue;
        }
        handler(request);
        put_done_request(threads, request);
    }

    return NULL;
}

static void add_free_request(Threads *threads, ThreadRequest *request)
{
    QSLIST_INSERT_HEAD(&threads->free_requests, request, node);
    threads->free_requests_nr++;
}

static ThreadRequest *get_and_remove_first_free_request(Threads *threads)
{
    ThreadRequest *request;

    if (QSLIST_EMPTY(&threads->free_requests)) {
        return NULL;
    }

    request = QSLIST_FIRST(&threads->free_requests);
    QSLIST_REMOVE_HEAD(&threads->free_requests, node);
    threads->free_requests_nr--;
    return request;
}

static void uninit_requests(Threads *threads, int free_nr)
{
    ThreadRequest *request;

    /*
     * all requests should be released to the list if threads are being
     * destroyed, i,e. should call threads_wait_done() first.
     */
    assert(threads->free_requests_nr == free_nr);

    while ((request = get_and_remove_first_free_request(threads))) {
        threads->thread_request_uninit(request);
    }

    assert(ptr_ring_empty(&threads->request_done_ring));
     ptr_ring_cleanup(&threads->request_done_ring, NULL);
}

static int init_requests(Threads *threads, int total_requests)
{
    ThreadRequest *request;
    int i, free_nr = 0;

    if (ptr_ring_init(&threads->request_done_ring, total_requests) < 0) {
        return -1;
    }
    ptr_ring_disable_batch(&threads->request_done_ring);

    QSLIST_INIT(&threads->free_requests);
    for (i = 0; i < total_requests; i++) {
        request = threads->thread_request_init();
        if (!request) {
            goto cleanup;
        }

        free_nr++;
        add_free_request(threads, request);
    }
    return 0;

cleanup:
    uninit_requests(threads, free_nr);
    return -1;
}

static void uninit_thread_data(Threads *threads, int num)
{
    ThreadLocal *thread_local = threads->per_thread_data;
    int i;

    for (i = 0; i < num; i++) {
        thread_local[i].quit = true;
        qemu_event_set(&thread_local[i].ev);
        qemu_thread_join(&thread_local[i].thread);
        qemu_event_destroy(&thread_local[i].ev);
        assert(ptr_ring_empty(&thread_local[i].request_ring));

        /* nothing is left in the ring. */
        ptr_ring_cleanup(&thread_local[i].request_ring, NULL);
    }
}

static int init_thread_data(Threads *threads, int threads_nr)
{
    ThreadLocal *thread_local = threads->per_thread_data;
    char *name;
    int i;

    for (i = 0; i < threads_nr; i++) {
        if (ptr_ring_init(&thread_local[i].request_ring,
                          threads->thread_ring_size) < 0) {
            goto exit;
        }
        ptr_ring_disable_batch(&thread_local[i].request_ring);

        qemu_event_init(&thread_local[i].ev, false);
        thread_local[i].threads = threads;
        thread_local[i].self = i;
        name = g_strdup_printf("%s/%d", threads->name, thread_local[i].self);
        qemu_thread_create(&thread_local[i].thread, name,
                           thread_run, &thread_local[i], QEMU_THREAD_JOINABLE);
        g_free(name);
    }
    return 0;

 exit:
    uninit_thread_data(threads, i);
    return -1;
}

Threads *threads_create(unsigned int threads_nr, const char *name,
                        int thread_ring_size,
                        ThreadRequest *(*thread_request_init)(void),
                        void (*thread_request_uninit)(ThreadRequest *request),
                        void (*thread_request_handler)(ThreadRequest *request),
                        void (*thread_request_done)(ThreadRequest *request))
{
    Threads *threads;
    int total_requests;

    threads = g_malloc0(sizeof(*threads) + threads_nr * sizeof(ThreadLocal));
    threads->name = name;
    threads->thread_request_init = thread_request_init;
    threads->thread_request_uninit = thread_request_uninit;
    threads->thread_request_handler = thread_request_handler;
    threads->thread_request_done = thread_request_done;
    qemu_spin_init(&threads->done_ring_lock);

    threads->thread_ring_size = thread_ring_size;
    total_requests = threads->thread_ring_size * threads_nr;
    if (init_requests(threads, total_requests) < 0) {
        goto exit;
    }
    threads->total_requests = total_requests;

    if (init_thread_data(threads, threads_nr) < 0) {
        goto exit;
    }
    threads->threads_nr = threads_nr;
    return threads;

exit:
    threads_destroy(threads);
    return NULL;
}

void threads_destroy(Threads *threads)
{
    uninit_thread_data(threads, threads->threads_nr);
    uninit_requests(threads, threads->total_requests);
    g_free(threads);
}

static int find_free_thread(Threads *threads, int range)
{
    int current_index, index, try = 0;

    current_index = threads->current_thread_index % threads->threads_nr;
    index = current_index;

    do {
        index = index % threads->threads_nr;
        if (!ptr_ring_full(&threads->per_thread_data[index].request_ring)) {
            threads->current_thread_index = index;
            return index;
        }

        if (++try > range) {
            return -1;
        }
    } while (++index != current_index);

    return -1;
}

ThreadRequest *threads_submit_request_prepare(Threads *threads)
{
    ThreadRequest *request;
    int index;

    /* seek a free one in all threads. */
    index = find_free_thread(threads, threads->threads_nr);
    if (index < 0) {
        return NULL;
    }

    /* try to get the request from the list */
    request = get_and_remove_first_free_request(threads);
    if (request) {
        goto got_request;
    }

    /* get the request already been handled by the threads */
    request = ptr_ring_consume(&threads->request_done_ring);
    if (request) {
        threads->thread_request_done(request);
        goto got_request;
    }

    return NULL;

got_request:
    request->thread_index = index;
    return request;
}

void threads_submit_request_commit(Threads *threads, ThreadRequest *request)
{
    int ret, index = request->thread_index;
    ThreadLocal *thread_local = &threads->per_thread_data[index];

    ret = ptr_ring_produce(&thread_local->request_ring, request);

    /*
     * we have detected that the thread's ring is not full in
     * threads_submit_request_prepare(), there should be free
     * room in the ring
     */
    assert(!ret);
    /* new request arrived, notify the thread */
    qemu_event_set(&thread_local->ev);

    /* we have used this entry, search from the next one. */
    threads->current_thread_index = ++index;
}

void threads_wait_done(Threads *threads)
{
    ThreadRequest *requests[DEFAULT_THREAD_RING_SIZE * 2];
    int nr;

retry:
    nr = ptr_ring_consume_batched(&threads->request_done_ring,
                                  (void **)requests, ARRAY_SIZE(requests));
    while (nr--) {
        threads->thread_request_done(requests[nr]);
       add_free_request(threads, requests[nr]);
    }

    if (threads->free_requests_nr != threads->total_requests) {
        cpu_relax();
        goto retry;
    }
}
