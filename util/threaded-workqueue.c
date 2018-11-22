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
    uint8_t done;

    /*
     * the index to Thread::requests.
     * Save it to the padding space although it can be calculated at runtime.
     */
    uint8_t request_index;

    /* the index to Threads::per_thread_data */
    unsigned int thread_index;
} QEMU_ALIGNED(sizeof(unsigned long));
typedef struct ThreadRequest ThreadRequest;

struct ThreadLocal {
    struct Threads *threads;

    /* the index of the thread */
    int self;

    /* thread is useless and needs to exit */
    bool quit;

    QemuThread thread;

    void *requests;

   /*
     * the bit in these two bitmaps indicates the index of the ＠requests
     * respectively. If it's the same, the corresponding request is free
     * and owned by the user, i.e, where the user fills a request. Otherwise,
     * it is valid and owned by the thread, i.e, where the thread fetches
     * the request and write the result.
     */

    /* after the user fills the request, the bit is flipped. */
    uint64_t request_fill_bitmap QEMU_ALIGNED(SMP_CACHE_BYTES);
    /* after handles the request, the thread flips the bit. */
    uint64_t request_done_bitmap QEMU_ALIGNED(SMP_CACHE_BYTES);

    /*
     * the event used to wake up the thread whenever a valid request has
     * been submitted
     */
    QemuEvent request_valid_ev QEMU_ALIGNED(SMP_CACHE_BYTES);

    /*
     * the event is notified whenever a request has been completed
     * (i.e, become free), which is used to wake up the user
     */
    QemuEvent request_free_ev QEMU_ALIGNED(SMP_CACHE_BYTES);
};
typedef struct ThreadLocal ThreadLocal;

/*
 * the main data struct represents multithreads which is shared by
 * all threads
 */
struct Threads {
    /* the request header, ThreadRequest, is contained */
    unsigned int request_size;
    unsigned int thread_requests_nr;
    unsigned int threads_nr;

    /* the request is pushed to the thread with round-robin manner */
    unsigned int current_thread_index;

    const ThreadedWorkqueueOps *ops;

    ThreadLocal per_thread_data[0];
};
typedef struct Threads Threads;

static ThreadRequest *index_to_request(ThreadLocal *thread, int request_index)
{
    ThreadRequest *request;

    request = thread->requests + request_index * thread->threads->request_size;
    assert(request->request_index == request_index);
    assert(request->thread_index == thread->self);
    return request;
}

static int request_to_index(ThreadRequest *request)
{
    return request->request_index;
}

static int request_to_thread_index(ThreadRequest *request)
{
    return request->thread_index;
}

/*
 * free request: the request is not used by any thread, however, it might
 *   contain the result need the user to call thread_request_done()
 *
 * valid request: the request contains the request data and it's committed
 *   to the thread, i,e. it's owned by thread.
 */
static uint64_t get_free_request_bitmap(Threads *threads, ThreadLocal *thread)
{
    uint64_t request_fill_bitmap, request_done_bitmap, result_bitmap;

    request_fill_bitmap = atomic_rcu_read(&thread->request_fill_bitmap);
    request_done_bitmap = atomic_rcu_read(&thread->request_done_bitmap);
    bitmap_xor(&result_bitmap, &request_fill_bitmap, &request_done_bitmap,
               threads->thread_requests_nr);

    /*
     * paired with smp_wmb() in mark_request_free() to make sure that we
     * read request_done_bitmap before fetching the result out.
     */
    smp_rmb();

    return result_bitmap;
}

static ThreadRequest
*find_thread_free_request(Threads *threads, ThreadLocal *thread)
{
    uint64_t result_bitmap = get_free_request_bitmap(threads, thread);
    int index;

    index  = find_first_zero_bit(&result_bitmap, threads->thread_requests_nr);
    if (index >= threads->thread_requests_nr) {
        return NULL;
    }

    return index_to_request(thread, index);
}

static ThreadRequest *threads_find_free_request(Threads *threads)
{
    ThreadLocal *thread;
    ThreadRequest *request;
    int cur_thread, thread_index;

    cur_thread = threads->current_thread_index % threads->threads_nr;
    thread_index = cur_thread;
    do {
        thread = threads->per_thread_data + thread_index++;
        request = find_thread_free_request(threads, thread);
        if (request) {
            break;
        }
        thread_index %= threads->threads_nr;
    } while (thread_index != cur_thread);

    return request;
}

/*
 * the change bit operation combined with READ_ONCE and WRITE_ONCE which
 * only works on single uint64_t width
 */
static void change_bit_once(long nr, uint64_t *addr)
{
    uint64_t value = atomic_rcu_read(addr) ^ BIT_MASK(nr);

    atomic_rcu_set(addr, value);
}

static void mark_request_valid(Threads *threads, ThreadRequest *request)
{
    int thread_index = request_to_thread_index(request);
    int request_index = request_to_index(request);
    ThreadLocal *thread = threads->per_thread_data + thread_index;

    /*
     * paired with smp_rmb() in find_first_valid_request_index() to make
     * sure the request has been filled before the bit is flipped that
     * will make the request be visible to the thread
     */
    smp_wmb();

    change_bit_once(request_index, &thread->request_fill_bitmap);
    qemu_event_set(&thread->request_valid_ev);
}

static int thread_find_first_valid_request_index(ThreadLocal *thread)
{
    Threads *threads = thread->threads;
    uint64_t request_fill_bitmap, request_done_bitmap, result_bitmap;
    int index;

    request_fill_bitmap = atomic_rcu_read(&thread->request_fill_bitmap);
    request_done_bitmap = atomic_rcu_read(&thread->request_done_bitmap);
    bitmap_xor(&result_bitmap, &request_fill_bitmap, &request_done_bitmap,
               threads->thread_requests_nr);
    /*
     * paired with smp_wmb() in mark_request_valid() to make sure that
     * we read request_fill_bitmap before fetch the request out.
     */
    smp_rmb();

    index = find_first_bit(&result_bitmap, threads->thread_requests_nr);
    return index >= threads->thread_requests_nr ? -1 : index;
}

static void mark_request_free(ThreadLocal *thread, ThreadRequest *request)
{
    int index = request_to_index(request);

    /*
     * smp_wmb() is implied in change_bit_atomic() that is paired with
     * smp_rmb() in get_free_request_bitmap() to make sure the result
     * has been saved before the bit is flipped.
     */
    change_bit_atomic(index, &thread->request_done_bitmap);
    qemu_event_set(&thread->request_free_ev);
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
            return index_to_request(thread, index);
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
        qemu_event_reset(&self_data->request_valid_ev);

        request = thread_busy_wait_for_request(self_data);
        if (!request) {
            qemu_event_wait(&self_data->request_valid_ev);
            continue;
        }

        assert(!request->done);

        handler(request + 1);
        request->done = true;
        mark_request_free(self_data, request);
    }

    return NULL;
}

static void uninit_thread_requests(ThreadLocal *thread, int free_nr)
{
    Threads *threads = thread->threads;
    ThreadRequest *request = thread->requests;
    int i;

    for (i = 0; i < free_nr; i++) {
        threads->ops->thread_request_uninit(request + 1);
        request = (void *)request + threads->request_size;
    }
    g_free(thread->requests);
}

static int init_thread_requests(ThreadLocal *thread)
{
    Threads *threads = thread->threads;
    ThreadRequest *request;
    int ret, i, thread_reqs_size;

    thread_reqs_size = threads->thread_requests_nr * threads->request_size;
    thread_reqs_size = QEMU_ALIGN_UP(thread_reqs_size, SMP_CACHE_BYTES);
    thread->requests = g_malloc0(thread_reqs_size);

    request = thread->requests;
    for (i = 0; i < threads->thread_requests_nr; i++) {
        ret = threads->ops->thread_request_init(request + 1);
        if (ret < 0) {
            goto exit;
        }

        request->request_index = i;
        request->thread_index = thread->self;
        request = (void *)request + threads->request_size;
    }
    return 0;

exit:
    uninit_thread_requests(thread, i);
    return -1;
}

static void uninit_thread_data(Threads *threads, int free_nr)
{
    ThreadLocal *thread_local = threads->per_thread_data;
    int i;

    for (i = 0; i < free_nr; i++) {
        thread_local[i].quit = true;
        qemu_event_set(&thread_local[i].request_valid_ev);
        qemu_thread_join(&thread_local[i].thread);
        qemu_event_destroy(&thread_local[i].request_valid_ev);
        qemu_event_destroy(&thread_local[i].request_free_ev);
        uninit_thread_requests(&thread_local[i], threads->thread_requests_nr);
    }
}

static int
init_thread_data(Threads *threads, const char *thread_name, int thread_nr)
{
    ThreadLocal *thread_local = threads->per_thread_data;
    char *name;
    int i;

    for (i = 0; i < thread_nr; i++) {
        thread_local[i].threads = threads;
        thread_local[i].self = i;

        if (init_thread_requests(&thread_local[i]) < 0) {
            goto exit;
        }

        qemu_event_init(&thread_local[i].request_free_ev, false);
        qemu_event_init(&thread_local[i].request_valid_ev, false);

        name = g_strdup_printf("%s/%d", thread_name, thread_local[i].self);
        qemu_thread_create(&thread_local[i].thread, name,
                           thread_run, &thread_local[i], QEMU_THREAD_JOINABLE);
        g_free(name);
    }
    return 0;

exit:
    uninit_thread_data(threads, i);
    return -1;
}

Threads *threaded_workqueue_create(const char *name, unsigned int threads_nr,
                                   unsigned int thread_requests_nr,
                                   const ThreadedWorkqueueOps *ops)
{
    Threads *threads;

    if (threads_nr > MAX_THREAD_REQUEST_NR) {
        return NULL;
    }

    threads = g_malloc0(sizeof(*threads) + threads_nr * sizeof(ThreadLocal));
    threads->ops = ops;
    threads->threads_nr = threads_nr;
    threads->thread_requests_nr = thread_requests_nr;

    QEMU_BUILD_BUG_ON(!QEMU_IS_ALIGNED(sizeof(ThreadRequest), sizeof(long)));
    threads->request_size = threads->ops->request_size;
    threads->request_size = QEMU_ALIGN_UP(threads->request_size, sizeof(long));
    threads->request_size += sizeof(ThreadRequest);

    if (init_thread_data(threads, name, threads_nr) < 0) {
        g_free(threads);
        return NULL;
    }

    return threads;
}

void threaded_workqueue_destroy(Threads *threads)
{
    uninit_thread_data(threads, threads->threads_nr);
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

    request = threads_find_free_request(threads);
    if (!request) {
        return NULL;
    }

    request_done(threads, request);
    return request + 1;
}

void threaded_workqueue_submit_request(Threads *threads, void *request)
{
    ThreadRequest *req = request - sizeof(ThreadRequest);
    int thread_index = request_to_thread_index(request);

    assert(!req->done);
    mark_request_valid(threads, req);
    threads->current_thread_index = thread_index  + 1;
}

void threaded_workqueue_wait_for_requests(Threads *threads)
{
    ThreadLocal *thread;
    uint64_t result_bitmap;
    int thread_index, index = 0;

    for (thread_index = 0; thread_index < threads->threads_nr; thread_index++) {
        thread = threads->per_thread_data + thread_index;
        index = 0;
retry:
        qemu_event_reset(&thread->request_free_ev);
        result_bitmap = get_free_request_bitmap(threads, thread);

        for (; index < threads->thread_requests_nr; index++) {
            if (test_bit(index, &result_bitmap)) {
                qemu_event_wait(&thread->request_free_ev);
                goto retry;
            }

            request_done(threads, index_to_request(thread, index));
        }
    }
}
