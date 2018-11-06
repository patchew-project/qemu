/*
 * Lockless and Efficient Threaded Workqueue Abstraction
 *
 * Author:
 *   Xiao Guangrong <xiaoguangrong@tencent.com>
 *
 * Copyright(C) 2018 Tencent Corporation.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "qemu/threaded-workqueue.h"

#define SMP_CACHE_BYTES 64
#define BITS_ALIGNED_TO_CACHE(_bits_)   \
    QEMU_ALIGN_UP(_bits_, SMP_CACHE_BYTES * BITS_PER_BYTE)

/*
 * the request representation which contains the internally used mete data,
 * it is the header of user-defined data.
 *
 * It should be aligned to the nature size of CPU.
 */
struct ThreadRequest {
    /*
     * the request has been handled by the thread and need the user
     * to fetch result out.
     */
    bool done;
    /*
     * the index to Threads::requests.
     * Save it to the padding space although it can be calculated at runtime.
     */
    int index;
};
typedef struct ThreadRequest ThreadRequest;

struct ThreadLocal {
    struct Threads *threads;

    /*
     * the request region in Threads::requests that the thread
     * need handle
     */
    int start_request_index;
    int end_request_index;

    /*
     * the interim bitmap used by the thread to avoid frequent
     * memory allocation
     */
    unsigned long *result_bitmap;

    /* the index of the thread */
    int self;

    /* thread is useless and needs to exit */
    bool quit;

    QemuThread thread;

    /* the event used to wake up the thread */
    QemuEvent ev;
};
typedef struct ThreadLocal ThreadLocal;

/*
 * the main data struct represents multithreads which is shared by
 * all threads
 */
struct Threads {
    /*
     * in order to avoid contention, the @requests is partitioned to
     * @threads_nr pieces, each thread exclusively handles
     * @thread_request_nr requests in the array.
     */
    void *requests;

    /*
     * the bit in these two bitmaps indicates the index of the ＠requests
     * respectively. If it's the same, the corresponding request is free
     * and owned by the user, i.e, where the user fills a request. Otherwise,
     * it is valid and owned by the thread, i.e, where the thread fetches
     * the request and write the result.
     */

    /* after the user fills the request, the bit is flipped. */
    unsigned long *request_fill_bitmap;
    /* after handles the request, the thread flips the bit. */
    unsigned long *request_done_bitmap;

    /*
     * the interim bitmap used by the user to avoid frequent
     * memory allocation
     */
    unsigned long *result_bitmap;

    /* the request header, ThreadRequest, is contained */
    unsigned int request_size;

    /* the number of requests that each thread need handle */
    unsigned int thread_request_nr;
    unsigned int total_requests;

    unsigned int threads_nr;

    /* the request is pushed to the thread with round-robin manner */
    unsigned int current_thread_index;

    ThreadedWorkqueueOps *ops;

    const char *name;
    QemuEvent ev;

    ThreadLocal per_thread_data[0];
};
typedef struct Threads Threads;

static ThreadRequest *index_to_request(Threads *threads, int request_index)
{
    ThreadRequest *request;

    request = threads->requests + request_index * threads->request_size;

    assert(request->index == request_index);
    return request;
}

static int request_to_index(ThreadRequest *request)
{
    return request->index;
}

static int thread_to_first_request_index(Threads *threads, int thread_id)
{
    thread_id %= threads->threads_nr;
    return thread_id * threads->thread_request_nr;
}

static int request_index_to_thread(Threads *threads, int request_index)
{
    return request_index / threads->thread_request_nr;
}

/*
 * free request: the request is not used by any thread, however, it might
 *   contian the result need the user to call thread_request_done()
 *
 * valid request: the request contains the request data and it's commited
 *   to the thread, i,e. it's owned by thread.
 */
static unsigned long *get_free_request_bitmap(Threads *threads)
{
    bitmap_xor(threads->result_bitmap, threads->request_fill_bitmap,
               threads->request_done_bitmap, threads->total_requests);

    /*
     * paired with smp_wmb() in mark_request_free() to make sure that we
     * read request_done_bitmap before fetch the result out.
     */
    smp_rmb();

    return threads->result_bitmap;
}

static int find_free_request_index(Threads *threads)
{
    unsigned long *result_bitmap = get_free_request_bitmap(threads);
    int index, cur_index;

    cur_index = thread_to_first_request_index(threads,
                                              threads->current_thread_index);

retry:
    index = find_next_zero_bit(result_bitmap, threads->total_requests,
                               cur_index);
    if (index < threads->total_requests) {
        return index;
    }

    /* if we get nothing, start it over. */
    if (cur_index != 0) {
        cur_index = 0;
        goto retry;
    }

    return -1;
}

static void mark_request_valid(Threads *threads, int request_index)
{
    /*
     * paired with smp_rmb() in find_first_valid_request_index() to make
     * sure the request has been filled before the bit is flipped that
     * will make the request be visible to the thread
     */
    smp_wmb();

    change_bit(request_index, threads->request_fill_bitmap);
}

static int thread_find_first_valid_request_index(ThreadLocal *thread)
{
    Threads *threads = thread->threads;
    int index;

    bitmap_xor(thread->result_bitmap, threads->request_fill_bitmap,
               threads->request_done_bitmap, threads->total_requests);
    /*
     * paired with smp_wmb() in mark_request_valid() to make sure that
     * we read request_fill_bitmap before fetch the request out.
     */
    smp_rmb();

    index = find_next_bit(thread->result_bitmap, threads->total_requests,
                          thread->start_request_index);
    return index > thread->end_request_index ? -1 : index;
}

static void mark_request_free(ThreadLocal *thread, ThreadRequest *request)
{
    int index = request_to_index(request);

    /*
     * smp_wmb() is implied in change_bit_atomic() that is paired with
     * smp_rmb() in get_free_request_bitmap() to make sure the result
     * has been saved before the bit is flipped.
     */
    change_bit_atomic(index, thread->threads->request_done_bitmap);
}

/* retry to see if there is available request before actually go to wait. */
#define BUSY_WAIT_COUNT 1000

static ThreadRequest *
thread_busy_wait_for_request(ThreadLocal *thread)
{
    int index, count = 0;

    for (count = 0; count < BUSY_WAIT_COUNT; count++) {
        index = thread_find_first_valid_request_index(thread);
        if (index >= 0) {
            assert(index >= thread->start_request_index &&
                   index <= thread->end_request_index);
            return index_to_request(thread->threads, index);
        }

        cpu_relax();
    }

    return NULL;
}

static void *thread_run(void *opaque)
{
    ThreadLocal *self_data = (ThreadLocal *)opaque;
    Threads *threads = self_data->threads;
    void (*handler)(void *request) = threads->ops->thread_request_handler;
    ThreadRequest *request;

    for ( ; !atomic_read(&self_data->quit); ) {
        qemu_event_reset(&self_data->ev);

        request = thread_busy_wait_for_request(self_data);
        if (!request) {
            qemu_event_wait(&self_data->ev);
            continue;
        }

        assert(!request->done);

        handler(request + 1);
        request->done = true;
        mark_request_free(self_data, request);
        qemu_event_set(&threads->ev);
    }

    return NULL;
}

static void uninit_requests(Threads *threads, int free_nr)
{
    ThreadRequest *request;
    int i;

    for (request = threads->requests, i = 0; i < free_nr; i++) {
        threads->ops->thread_request_uninit(request + 1);
        request = (void *)request + threads->request_size;
    }

    g_free(threads->result_bitmap);
    g_free(threads->request_fill_bitmap);
    g_free(threads->request_done_bitmap);
    g_free(threads->requests);
}

static int init_requests(Threads *threads)
{
    ThreadRequest *request;
    int aligned_requests, free_nr = 0, ret = -1;

    aligned_requests = BITS_ALIGNED_TO_CACHE(threads->total_requests);
    threads->request_fill_bitmap = bitmap_new(aligned_requests);
    threads->request_done_bitmap = bitmap_new(aligned_requests);
    threads->result_bitmap = bitmap_new(threads->total_requests);

    QEMU_BUILD_BUG_ON(!QEMU_IS_ALIGNED(sizeof(ThreadRequest), sizeof(long)));

    threads->request_size = threads->ops->thread_get_request_size();
    threads->request_size = QEMU_ALIGN_UP(threads->request_size, sizeof(long));
    threads->request_size += sizeof(ThreadRequest);
    threads->requests = g_try_malloc0_n(threads->total_requests,
                                        threads->request_size);
    if (!threads->requests) {
        goto exit;
    }

    for (request = threads->requests; free_nr < threads->total_requests;
        free_nr++) {
        ret = threads->ops->thread_request_init(request + 1);
        if (ret < 0) {
            goto exit;
        }

        request->index = free_nr;
        request = (void *)request + threads->request_size;
    }

    return 0;

exit:
    uninit_requests(threads, free_nr);
    return ret;
}

static void uninit_thread_data(Threads *threads)
{
    ThreadLocal *thread_local = threads->per_thread_data;
    int i;

    for (i = 0; i < threads->threads_nr; i++) {
        thread_local[i].quit = true;
        qemu_event_set(&thread_local[i].ev);
        qemu_thread_join(&thread_local[i].thread);
        qemu_event_destroy(&thread_local[i].ev);
        g_free(thread_local[i].result_bitmap);
    }
}

static void init_thread_data(Threads *threads)
{
    ThreadLocal *thread_local = threads->per_thread_data;
    char *name;
    int start_index, end_index, i;

    for (i = 0; i < threads->threads_nr; i++) {
        thread_local[i].threads = threads;
        thread_local[i].self = i;

        start_index = thread_to_first_request_index(threads, i);
        end_index = start_index + threads->thread_request_nr - 1;
        thread_local[i].start_request_index = start_index;
        thread_local[i].end_request_index = end_index;

        thread_local[i].result_bitmap = bitmap_new(threads->total_requests);

        qemu_event_init(&thread_local[i].ev, false);

        name = g_strdup_printf("%s/%d", threads->name, thread_local[i].self);
        qemu_thread_create(&thread_local[i].thread, name,
                           thread_run, &thread_local[i], QEMU_THREAD_JOINABLE);
        g_free(name);
    }
}

Threads *threaded_workqueue_create(const char *name, unsigned int threads_nr,
                               int thread_request_nr, ThreadedWorkqueueOps *ops)
{
    Threads *threads;

    threads = g_malloc0(sizeof(*threads) + threads_nr * sizeof(ThreadLocal));
    threads->name = name;
    threads->ops = ops;

    threads->threads_nr = threads_nr;
    threads->thread_request_nr = thread_request_nr;

    threads->total_requests = thread_request_nr * threads_nr;
    if (init_requests(threads) < 0) {
        g_free(threads);
        return NULL;
    }

    qemu_event_init(&threads->ev, false);
    init_thread_data(threads);
    return threads;
}

void threaded_workqueue_destroy(Threads *threads)
{
    uninit_thread_data(threads);
    uninit_requests(threads, threads->total_requests);
    qemu_event_destroy(&threads->ev);
    g_free(threads);
}

static void request_done(Threads *threads, ThreadRequest *request)
{
    if (!request->done) {
        return;
    }

    threads->ops->thread_request_done(request + 1);
    request->done = false;
}

void *threaded_workqueue_get_request(Threads *threads)
{
    ThreadRequest *request;
    int index;

    index = find_free_request_index(threads);
    if (index < 0) {
        return NULL;
    }

    request = index_to_request(threads, index);
    request_done(threads, request);
    return request + 1;
}

void threaded_workqueue_submit_request(Threads *threads, void *request)
{
    ThreadRequest *req = request - sizeof(ThreadRequest);
    int request_index = request_to_index(req);
    int thread_index = request_index_to_thread(threads, request_index);
    ThreadLocal *thread_local = &threads->per_thread_data[thread_index];

    assert(!req->done);

    mark_request_valid(threads, request_index);

    threads->current_thread_index = ++thread_index;
    qemu_event_set(&thread_local->ev);
}

void threaded_workqueue_wait_for_requests(Threads *threads)
{
    unsigned long *result_bitmap;
    int index = 0;

retry:
    qemu_event_reset(&threads->ev);
    result_bitmap = get_free_request_bitmap(threads);
    for (; index < threads->total_requests; index++) {
        if (test_bit(index, result_bitmap)) {
            qemu_event_wait(&threads->ev);
            goto retry;
        };

        request_done(threads, index_to_request(threads, index));
    }
}
