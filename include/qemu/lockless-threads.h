/*
 * Lockless Multithreads Abstraction
 *
 * This is the abstraction layer for lockless multithreads management.
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

#ifndef QEMU_LOCKLESS_THREAD_H
#define QEMU_LOCKLESS_THREAD_H

#include "qemu/queue.h"
#include "qemu/thread.h"
#include "qemu/ptr_ring.h"

/*
 * the request representation which contains the internally used mete data,
 * it can be embedded to user's self-defined data struct and the user can
 * use container_of() to get the self-defined data
 */
struct ThreadRequest {
    QSLIST_ENTRY(ThreadRequest) node;
    unsigned int thread_index;
};
typedef struct ThreadRequest ThreadRequest;

typedef struct Threads Threads;

/* the size of thread local request ring on default */
#define DEFAULT_THREAD_RING_SIZE 4

Threads *threads_create(unsigned int threads_nr, const char *name,
                        int thread_ring_size,
                        ThreadRequest *(*thread_request_init)(void),
                        void (*thread_request_uninit)(ThreadRequest *request),
                        void (*thread_request_handler)(ThreadRequest *request),
                        void (*thread_request_done)(ThreadRequest *request));
void threads_destroy(Threads *threads);

/*
 * find a free request and associate it with a free thread.
 * If no request or no thread is free, return NULL
 */
ThreadRequest *threads_submit_request_prepare(Threads *threads);
/*
 * push the request to its thread's local ring and notify the thread
 */
void threads_submit_request_commit(Threads *threads, ThreadRequest *request);

/*
 * wait all threads to complete the request filled in their local rings
 * to make sure there is no previous request exists.
 */
void threads_wait_done(Threads *threads);
#endif
