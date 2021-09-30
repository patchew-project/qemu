/*
 * custom threadpool for virtiofsd
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * Authors:
 *     Ioannis Angelakopoulos <iangelak@redhat.com>
 *     Vivek Goyal <vgoyal@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <pthread.h>
#include <glib.h>
#include <stdbool.h>
#include <errno.h>
#include "tpool.h"
#include "fuse_log.h"

struct fv_PoolReq {
    struct fv_PoolReq *next;                        /* pointer to next task */
    void (*worker_func)(void *arg1, void *arg2);    /* worker function */
    void *arg1;                                     /* 1st arg: Request */
    void *arg2;                                     /* 2nd arg: Virtqueue */
};

struct fv_PoolReqQueue {
    pthread_mutex_t lock;
    GQueue queue;
    pthread_cond_t notify;                         /* Conditional variable */
};

struct fv_PoolThread {
    pthread_t pthread;
    int alive;
    int id;
    struct fv_ThreadPool *tpool;
};

struct fv_ThreadPool {
    struct fv_PoolThread **threads;
    struct fv_PoolReqQueue *req_queue;
    pthread_mutex_t tp_lock;

    /* Total number of threads created */
    int num_threads;

    /* Number of threads running now */
    int nr_running;
    int destroy_pool;
};

/* Initialize the Locking Request Queue */
static struct fv_PoolReqQueue *fv_pool_request_queue_init(void)
{
    struct fv_PoolReqQueue *rq;

    rq = g_new0(struct fv_PoolReqQueue, 1);
    pthread_mutex_init(&(rq->lock), NULL);
    pthread_cond_init(&(rq->notify), NULL);
    g_queue_init(&rq->queue);
    return rq;
}

/* Push a new locking request to the queue*/
void fv_thread_pool_push(struct fv_ThreadPool *tpool,
                         void (*worker_func)(void *, void *),
                         void *arg1, void *arg2)
{
    struct fv_PoolReq *newreq;
    struct fv_PoolReqQueue *rq = tpool->req_queue;

    newreq = g_new(struct fv_PoolReq, 1);
    newreq->worker_func = worker_func;
    newreq->arg1 = arg1;
    newreq->arg2 = arg2;
    newreq->next = NULL;

    /* Now add the request to the queue */
    pthread_mutex_lock(&rq->lock);
    g_queue_push_tail(&rq->queue, newreq);

    /* Notify the threads that a request is available */
    pthread_cond_signal(&rq->notify);
    pthread_mutex_unlock(&rq->lock);

}

/* Pop a locking request from the queue*/
static struct fv_PoolReq *fv_tpool_pop(struct fv_ThreadPool *tpool)
{
    struct fv_PoolReq *pool_req = NULL;
    struct fv_PoolReqQueue *rq = tpool->req_queue;

    pthread_mutex_lock(&rq->lock);

    pool_req = g_queue_pop_head(&rq->queue);

    if (!g_queue_is_empty(&rq->queue)) {
        pthread_cond_signal(&rq->notify);
    }
    pthread_mutex_unlock(&rq->lock);

    return pool_req;
}

static void fv_pool_request_queue_destroy(struct fv_ThreadPool *tpool)
{
    struct fv_PoolReq *pool_req;

    while ((pool_req = fv_tpool_pop(tpool))) {
        g_free(pool_req);
    }

    /* Now free the actual queue itself */
    g_free(tpool->req_queue);
}

/*
 * Signal handler for blcking threads that wait on a remote lock to be released
 * Called when virtiofsd does cleanup and wants to wake up these threads
 */
static void fv_thread_signal_handler(int signal)
{
    fuse_log(FUSE_LOG_DEBUG, "Thread received a signal.\n");
    return;
}

static bool is_pool_stopping(struct fv_ThreadPool *tpool)
{
    bool destroy = false;

    pthread_mutex_lock(&tpool->tp_lock);
    destroy = tpool->destroy_pool;
    pthread_mutex_unlock(&tpool->tp_lock);

    return destroy;
}

static void *fv_thread_do_work(void *thread)
{
    struct fv_PoolThread *worker = (struct fv_PoolThread *)thread;
    struct fv_ThreadPool *tpool = worker->tpool;
    struct fv_PoolReq *pool_request;
    /* Actual worker function and arguments. Same as non locking requests */
    void (*worker_func)(void*, void*);
    void *arg1;
    void *arg2;

    while (1) {
        if (is_pool_stopping(tpool)) {
            break;
        }

        /*
         * Get the queue lock first so that we can wait on the conditional
         * variable afterwards
         */
        pthread_mutex_lock(&tpool->req_queue->lock);

        /* Wait on the condition variable until it is available */
        while (g_queue_is_empty(&tpool->req_queue->queue) &&
               !is_pool_stopping(tpool)) {
            pthread_cond_wait(&tpool->req_queue->notify,
                              &tpool->req_queue->lock);
        }

        /* Unlock the queue for other threads */
        pthread_mutex_unlock(&tpool->req_queue->lock);

        if (is_pool_stopping(tpool)) {
            break;
        }

        /* Now the request must be serviced */
        pool_request = fv_tpool_pop(tpool);
        if (pool_request) {
            fuse_log(FUSE_LOG_DEBUG, "%s: Locking Thread:%d handling"
                    " a request\n", __func__, worker->id);
            worker_func = pool_request->worker_func;
            arg1 = pool_request->arg1;
            arg2 = pool_request->arg2;
            worker_func(arg1, arg2);
            g_free(pool_request);
        }
    }

    /* Mark the thread as inactive */
    pthread_mutex_lock(&tpool->tp_lock);
    tpool->threads[worker->id]->alive = 0;
    tpool->nr_running--;
    pthread_mutex_unlock(&tpool->tp_lock);

    return NULL;
}

/* Create a single thread that handles locking requests */
static int fv_worker_thread_init(struct fv_ThreadPool *tpool,
                                 struct fv_PoolThread **thread, int id)
{
    struct fv_PoolThread *worker;
    int ret;

    worker = g_new(struct fv_PoolThread, 1);
    worker->tpool = tpool;
    worker->id = id;
    worker->alive = 1;

    ret = pthread_create(&worker->pthread, NULL, fv_thread_do_work,
                         worker);
    if (ret) {
        fuse_log(FUSE_LOG_ERR, "pthread_create() failed with err=%d\n", ret);
        g_free(worker);
        return ret;
    }
    pthread_detach(worker->pthread);
    *thread = worker;
    return 0;
}

static void send_signal_all(struct fv_ThreadPool *tpool)
{
    int i;

    pthread_mutex_lock(&tpool->tp_lock);
    for (i = 0; i < tpool->num_threads; i++) {
        if (tpool->threads[i]->alive) {
            pthread_kill(tpool->threads[i]->pthread, SIGUSR1);
        }
    }
    pthread_mutex_unlock(&tpool->tp_lock);
}

static void do_pool_destroy(struct fv_ThreadPool *tpool, bool send_signal)
{
    int i, nr_running;

    /* We want to destroy the pool */
    pthread_mutex_lock(&tpool->tp_lock);
    tpool->destroy_pool = 1;
    pthread_mutex_unlock(&tpool->tp_lock);

    /* Wake up threads waiting for requests */
    pthread_mutex_lock(&tpool->req_queue->lock);
    pthread_cond_broadcast(&tpool->req_queue->notify);
    pthread_mutex_unlock(&tpool->req_queue->lock);

    /* Send Signal and wait for all threads to exit. */
    while (1) {
        if (send_signal) {
            send_signal_all(tpool);
        }
        pthread_mutex_lock(&tpool->tp_lock);
        nr_running = tpool->nr_running;
        pthread_mutex_unlock(&tpool->tp_lock);
        if (!nr_running) {
            break;
        }
        g_usleep(10000);
    }

    /* Destroy the locking request queue */
    fv_pool_request_queue_destroy(tpool);
    for (i = 0; i < tpool->num_threads; i++) {
        g_free(tpool->threads[i]);
    }

    /* Now free the threadpool */
    g_free(tpool->threads);
    g_free(tpool);
}

void fv_thread_pool_destroy(struct fv_ThreadPool *tpool)
{
    if (!tpool) {
        return;
    }
    do_pool_destroy(tpool, true);
}

static int register_sig_handler(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = fv_thread_signal_handler;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        fuse_log(FUSE_LOG_ERR, "Cannot register the signal handler:%s\n",
                 strerror(errno));
        return 1;
    }
    return 0;
}

/* Initialize the thread pool for the locking posix threads */
struct fv_ThreadPool *fv_thread_pool_init(unsigned int thread_num)
{
    struct fv_ThreadPool *tpool = NULL;
    int i, ret;

    if (!thread_num) {
        thread_num = 1;
    }

    if (register_sig_handler()) {
        return NULL;
    }
    tpool = g_new0(struct fv_ThreadPool, 1);
    pthread_mutex_init(&(tpool->tp_lock), NULL);

    /* Initialize the Lock Request Queue */
    tpool->req_queue = fv_pool_request_queue_init();

    /* Create the threads in the pool */
    tpool->threads = g_new(struct fv_PoolThread *, thread_num);

    for (i = 0; i < thread_num; i++) {
        ret = fv_worker_thread_init(tpool, &tpool->threads[i], i);
        if (ret) {
            goto out_err;
        }
        tpool->num_threads++;
        tpool->nr_running++;
    }

    return tpool;
out_err:
    /* An error occurred. Cleanup and return NULL */
    do_pool_destroy(tpool, false);
    return NULL;
}
