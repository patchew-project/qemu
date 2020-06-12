#include "qemu/osdep.h"
#include "qemu.h"
#include "clone.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>

static const unsigned long NEW_STACK_SIZE = 0x40000UL;

/*
 * A completion tracks an event that can be completed. It's based on the
 * kernel concept with the same name, but implemented with userspace locks.
 */
struct completion {
    /* done is set once this completion has been completed. */
    bool done;
    /* mu syncronizes access to this completion. */
    pthread_mutex_t mu;
    /* cond is used to broadcast completion status to awaiting threads. */
    pthread_cond_t cond;
};

static void completion_init(struct completion *c)
{
    c->done = false;
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->cond, NULL);
}

/*
 * Block until the given completion finishes. Returns immediately if the
 * completion has already finished.
 */
static void completion_await(struct completion *c)
{
    pthread_mutex_lock(&c->mu);
    if (c->done) {
        pthread_mutex_unlock(&c->mu);
        return;
    }
    pthread_cond_wait(&c->cond, &c->mu);
    assert(c->done && "returned from cond wait without being marked as done");
    pthread_mutex_unlock(&c->mu);
}

/*
 * Finish the completion. Unblocks all awaiters.
 */
static void completion_finish(struct completion *c)
{
    pthread_mutex_lock(&c->mu);
    assert(!c->done && "trying to finish an already finished completion");
    c->done = true;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->mu);
}

struct clone_thread_info {
    struct completion running;
    int tid;
    int (*callback)(void *);
    void *payload;
};

static void *clone_thread_run(void *raw_info)
{
    struct clone_thread_info *info = (struct clone_thread_info *) raw_info;
    info->tid = syscall(SYS_gettid);

    /*
     * Save out callback/payload since lifetime of info is only guaranteed
     * until we finish the completion.
     */
    int (*callback)(void *) = info->callback;
    void *payload = info->payload;
    completion_finish(&info->running);

    _exit(callback(payload));
}

static int clone_thread(int flags, int (*callback)(void *), void *payload)
{
    struct clone_thread_info info;
    pthread_attr_t attr;
    int ret;
    pthread_t thread_unused;

    memset(&info, 0, sizeof(info));

    completion_init(&info.running);
    info.callback = callback;
    info.payload = payload;

    (void)pthread_attr_init(&attr);
    (void)pthread_attr_setstacksize(&attr, NEW_STACK_SIZE);
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    ret = pthread_create(&thread_unused, &attr, clone_thread_run, (void *) &info);
    /* pthread_create returns errors directly, instead of via errno. */
    if (ret != 0) {
        errno = ret;
        ret = -1;
    } else {
        completion_await(&info.running);
        ret = info.tid;
    }

    pthread_attr_destroy(&attr);
    return ret;
}

int qemu_clone(int flags, int (*callback)(void *), void *payload)
{
    int ret;

    if (clone_flags_are_thread(flags)) {
        /*
         * The new process uses the same flags as pthread_create, so we can
         * use pthread_create directly. This is an optimization.
         */
        return clone_thread(flags, callback, payload);
    }

    if (clone_flags_are_fork(flags)) {
        /*
         * Special case a true `fork` clone call. This is so we can take
         * advantage of special pthread_atfork handlers in libraries we
         * depend on (e.g., glibc). Without this, existing users of `fork`
         * in multi-threaded environments will likely get new flaky
         * deadlocks.
         */
        fork_start();
        ret = fork();
        if (ret == 0) {
            fork_end(1);
            _exit(callback(payload));
        }
        fork_end(0);
        return ret;
    }

    /* !fork && !thread */
    errno = EINVAL;
    return -1;
}
