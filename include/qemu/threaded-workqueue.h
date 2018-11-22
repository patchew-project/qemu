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

#ifndef QEMU_THREADED_WORKQUEUE_H
#define QEMU_THREADED_WORKQUEUE_H

#include "qemu/queue.h"
#include "qemu/thread.h"

/*
 * This modules implements the lockless and efficient threaded workqueue.
 *
 * Three abstracted objects are used in this module:
 * - Request.
 *   It not only contains the data that the workqueue fetches out
 *   to finish the request but also offers the space to save the result
 *   after the workqueue handles the request.
 *
 *   It's flowed between user and workqueue. The user fills the request
 *   data into it when it is owned by user. After it is submitted to the
 *   workqueue, the workqueue fetched data out and save the result into
 *   it after the request is handled.
 *
 *   All the requests are pre-allocated and carefully partitioned between
 *   threads so there is no contention on the request, that make threads
 *   be parallel as much as possible.
 *
 * - User, i.e, the submitter
 *   It's the one fills the request and submits it to the workqueue,
 *   the result will be collected after it is handled by the work queue.
 *
 *   The user can consecutively submit requests without waiting the previous
 *   requests been handled.
 *   It only supports one submitter, you should do serial submission by
 *   yourself if you want more, e.g, use lock on you side.
 *
 * - Workqueue, i.e, thread
 *   Each workqueue is represented by a running thread that fetches
 *   the request submitted by the user, do the specified work and save
 *   the result to the request.
 */

typedef struct Threads Threads;

struct ThreadedWorkqueueOps {
    /* constructor of the request */
    int (*thread_request_init)(void *request);
    /*  destructor of the request */
    void (*thread_request_uninit)(void *request);

    /* the handler of the request that is called by the thread */
    void (*thread_request_handler)(void *request);
    /* called by the user after the request has been handled */
    void (*thread_request_done)(void *request);

    size_t request_size;
};
typedef struct ThreadedWorkqueueOps ThreadedWorkqueueOps;

/* the default number of requests that thread need handle */
#define DEFAULT_THREAD_REQUEST_NR 4
/* the max number of requests that thread need handle */
#define MAX_THREAD_REQUEST_NR     (sizeof(uint64_t) * BITS_PER_BYTE)

/*
 * create a threaded queue. Other APIs will work on the Threads it returned
 *
 * @name: the identity of the workqueue which is used to construct the name
 *    of threads only
 * @threads_nr: the number of threads that the workqueue will create
 * @thread_requests_nr: the number of requests that each single thread will
 *    handle
 * @ops: the handlers of the request
 *
 * Return NULL if it failed
 */
Threads *threaded_workqueue_create(const char *name, unsigned int threads_nr,
                                   unsigned int thread_requests_nr,
                                   const ThreadedWorkqueueOps *ops);
void threaded_workqueue_destroy(Threads *threads);

/*
 * find a free request where the user can store the data that is needed to
 * finish the request
 *
 * If all requests are used up, return NULL
 */
void *threaded_workqueue_get_request(Threads *threads);
/* submit the request and notify the thread */
void threaded_workqueue_submit_request(Threads *threads, void *request);

/*
 * wait all threads to complete the request to make sure there is no
 * previous request exists
 */
void threaded_workqueue_wait_for_requests(Threads *threads);
#endif
