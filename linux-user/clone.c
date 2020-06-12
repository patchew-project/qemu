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

/* arch-specifc includes needed to fetch the TLS base offset. */
#if defined(__x86_64__)
#include <asm/prctl.h>
#include <sys/prctl.h>
#endif

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

struct tls_manager {
    void *tls_ptr;
    /* fetched is completed once tls_ptr has been set by the thread. */
    struct completion fetched;
    /*
     * spawned is completed by the user once the managed_tid
     * has been spawned.
     */
    struct completion spawned;
    /*
     * TID of the child whose memory is cleaned up upon death. This memory
     * location is used as part of a futex op, and is cleared by the kernel
     * since we specify CHILD_CLEARTID.
     */
    int managed_tid;
    /*
     * The value to be `free`'d up once the janitor is ready to clean up the
     * TLS section, and the managed tid has exited.
     */
    void *cleanup;
};

/*
 * tls_ptr fetches the TLS "pointer" for the current thread. This pointer
 * should be whatever platform-specific address is used to represent the TLS
 * base address.
 */
static void *tls_ptr()
{
    void *ptr;
#if defined(__x86_64__)
    /*
     * On x86_64, the TLS base is stored in the `fs` segment register, we can
     * fetch it with `ARCH_GET_FS`:
     */
    (void)syscall(SYS_arch_prctl, ARCH_GET_FS, (unsigned long) &ptr);
#else
    ptr = NULL;
#endif
    return ptr;
}

/*
 * clone_vm_supported returns true if clone_vm() is supported on this
 * platform.
 */
static bool clone_vm_supported()
{
#if defined(__x86_64__)
    return true;
#else
    return false;
#endif
}

static void *tls_manager_thread(void *arg)
{
    struct tls_manager *mgr = (struct tls_manager *) arg;
    int child_tid, ret;

    /*
     * NOTE: Do not use an TLS in this thread until after the `spawned`
     * completion is finished. We need to preserve the pristine state of
     * the TLS image for this thread, so it can be re-used in a separate
     * process.
     */
    mgr->tls_ptr = tls_ptr();

    /* Notify tls_new that we finished fetching the TLS ptr. */
    completion_finish(&mgr->fetched);

    /*
     * Wait for the user of our TLS to tell us the child using our TLS has
     * been spawned.
     */
    completion_await(&mgr->spawned);

    child_tid = atomic_fetch_or(&mgr->managed_tid, 0);
    /*
     * Check if the child has already terminated by this point. If not, wait
     * for the child to exit. As long as the trampoline is not killed by
     * a signal, the kernel guarantees that the memory at &mgr->managed_tid
     * will be cleared, and a FUTEX_WAKE at that address will triggered.
     */
    if (child_tid != 0) {
        ret = syscall(SYS_futex, &mgr->managed_tid, FUTEX_WAIT,
                      child_tid, NULL, NULL, 0);
        assert(ret == 0 && "clone manager futex should always succeed");
    }

    free(mgr->cleanup);
    g_free(mgr);

    return NULL;
}

static struct tls_manager *tls_manager_new()
{
    struct tls_manager *mgr = g_new0(struct tls_manager, 1);
    sigset_t block, oldmask;

    sigfillset(&block);
    if (sigprocmask(SIG_BLOCK, &block, &oldmask) != 0) {
        return NULL;
    }

    completion_init(&mgr->fetched);
    completion_init(&mgr->spawned);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_t unused;
    if (pthread_create(&unused, &attr, tls_manager_thread, (void *) mgr)) {
        pthread_attr_destroy(&attr);
        g_free(mgr);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    completion_await(&mgr->fetched);

    if (sigprocmask(SIG_SETMASK, &oldmask, NULL) != 0) {
        /* Let the thread exit, and cleanup itself. */
        completion_finish(&mgr->spawned);
        return NULL;
    }

    /* Once we finish awaiting, the tls_ptr will be usable. */
    return mgr;
}

struct stack {
    /* Buffer is the "base" of the stack buffer. */
    void *buffer;
    /* Top is the "start" of the stack (since stack addresses "grow down"). */
    void *top;
};

struct info {
    /* Stacks used for the trampoline and child process. */
    struct {
        struct stack trampoline;
        struct stack process;
    } stack;
    struct completion child_ready;
    /* `clone` flags for the process the user asked us to make. */
    int flags;
    sigset_t orig_mask;
    /*
     * Function to run in the ultimate child process, and payload to pass as
     * the argument.
     */
    int (*clone_f)(void *);
    void *payload;
    /*
     * Result of calling `clone` for the child clone. Will be set to
     * `-errno` if an error occurs.
     */
    int result;
};

static bool stack_new(struct stack *stack)
{
    /*
     * TODO: put a guard page at the bottom of the stack, so we don't
     * accidentally roll off the end.
     */
    if (posix_memalign(&stack->buffer, 16, NEW_STACK_SIZE)) {
        return false;
    }
    memset(stack->buffer, 0, NEW_STACK_SIZE);
    stack->top = stack->buffer + NEW_STACK_SIZE;
    return true;
}

static int clone_child(void *raw_info)
{
    struct info *info = (struct info *) raw_info;
    int (*clone_f)(void *) = info->clone_f;
    void *payload = info->payload;
    if (!(info->flags & CLONE_VFORK)) {
        /*
         * If CLONE_VFORK is NOT set, then the trampoline has stalled (it
         * forces VFORK), but the actual clone should return immediately. In
         * this case, this thread needs to notify the parent that the new
         * process is running. If CLONE_VFORK IS set, the trampoline will
         * notify the parent once the normal kernel vfork completes.
         */
        completion_finish(&info->child_ready);
    }
    if (sigprocmask(SIG_SETMASK, &info->orig_mask, NULL) != 0) {
        perror("failed to restore signal mask in cloned child");
        _exit(1);
    }
    return clone_f(payload);
}

static int clone_trampoline(void *raw_info)
{
    struct info *info = (struct info *) raw_info;
    int flags;

    struct stack process_stack = info->stack.process;
    int orig_flags = info->flags;

    if (orig_flags & CSIGNAL) {
        /*
         * It should be safe to call here, since we know signals are blocked
         * for this process.
         */
        hide_current_process_exit_signal();
    }

    /*
     * Force CLONE_PARENT, so that we don't accidentally become a child of the
     * trampoline thread. This kernel task should either be a child of the
     * trampoline's parent (if CLONE_PARENT is not in info->flags), or a child
     * of the calling process's parent (if CLONE_PARENT IS in info->flags).
     * That is to say, our parent should always be the correct parent for the
     * child task.
     *
     * Force CLONE_VFORK so that we know when the child is no longer holding
     * a reference to this process's virtual memory. CLONE_VFORK just suspends
     * this task until the child execs or exits, it should not affect how the
     * child process is created in any way. This is the only generic way I'm
     * aware of to observe *any* exit or exec. Including "abnormal" exits like
     * exits via signals.
     *
     * Force CLONE_CHILD_SETTID, since we want to track the CHILD TID in the
     * `info` structure. Capturing the child via `clone` call directly is
     * slightly nicer than making a syscall in the child. Since we know we're
     * doing a CLONE_VM here, we can use CLONE_CHILD_SETTID, to guarantee that
     * the kernel must set the child TID before the child is run. The child
     * TID should be visibile to the parent, since both parent and child share
     * and address space. If the clone fails, we overwrite `info->result`
     * anyways with the error code.
     */
    flags = orig_flags | CLONE_PARENT | CLONE_VFORK | CLONE_CHILD_SETTID;
    if (clone(clone_child, info->stack.process.top, flags,
              (void *) info, NULL, NULL, &info->result) < 0) {
        info->result = -errno;
        completion_finish(&info->child_ready);
        return 0;
    }

    /*
     * Clean up the child process stack, since we know the child can no longer
     * reference it.
     */
    free(process_stack.buffer);

    /*
     * We know the process we created was CLONE_VFORK, so it registered with
     * the RCU. We share a TLS image with the process, so we can unregister
     * it from the RCU. Since the TLS image will be valid for at least our
     * lifetime, it should be OK to leave the child processes RCU entry in
     * the queue between when the child execve or exits, and the OS returns
     * here from our vfork.
     */
    rcu_unregister_thread();

    /*
     * If we're doing a real vfork here, we need to notify the parent that the
     * vfork has happened.
     */
    if (orig_flags & CLONE_VFORK) {
        completion_finish(&info->child_ready);
    }

    return 0;
}

static int clone_vm(int flags, int (*callback)(void *), void *payload)
{
    struct info info;
    sigset_t sigmask;
    int ret;

    assert(flags & CLONE_VM && "CLONE_VM flag must be set");

    memset(&info, 0, sizeof(info));
    info.clone_f = callback;
    info.payload = payload;
    info.flags = flags;

    /*
     * Set up the stacks for the child processes needed to execute the clone.
     */
    if (!stack_new(&info.stack.trampoline)) {
        return -1;
    }
    if (!stack_new(&info.stack.process)) {
        free(info.stack.trampoline.buffer);
        return -1;
    }

    /*
     * tls_manager_new grants us it's ownership of the reference to the
     * TLS manager, so we "leak" the data pointer, instead of using _get()
     */
    struct tls_manager *mgr = tls_manager_new();
    if (mgr == NULL) {
        free(info.stack.trampoline.buffer);
        free(info.stack.process.buffer);
        return -1;
    }

    /* Manager cleans up the trampoline stack once the trampoline exits. */
    mgr->cleanup = info.stack.trampoline.buffer;

    /*
     * Flags used by the trampoline in the 2-phase clone setup for children
     * cloned with CLONE_VM. We want the trampoline to be essentially identical
     * to its parent. This improves the performance of cloning the trampoline,
     * and guarantees that the real flags are implemented correctly.
     *
     * CLONE_CHILD_SETTID: Make the kernel set the managed_tid for the TLS
     * manager.
     *
     * CLONE_CHILD_CLEARTID: Make the kernel clear the managed_tid, and
     * trigger a FUTEX_WAKE (received by the TLS manager), so the TLS manager
     * knows when to cleanup the trampoline stack.
     *
     * CLONE_SETTLS: To set the trampoline TLS based on the tls manager.
     */
    static const int base_trampoline_flags = (
        CLONE_FILES | CLONE_FS | CLONE_IO | CLONE_PTRACE |
        CLONE_SIGHAND | CLONE_SYSVSEM | CLONE_VM
    ) | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID | CLONE_SETTLS;

    int trampoline_flags = base_trampoline_flags;

    /*
     * To get the process hierarchy right, we set the trampoline
     * CLONE_PARENT/CLONE_THREAD flag to match the child
     * CLONE_PARENT/CLONE_THREAD. So add those flags if specified by the child.
     */
    trampoline_flags |= (flags & CLONE_PARENT) ? CLONE_PARENT : 0;
    trampoline_flags |= (flags & CLONE_THREAD) ? CLONE_THREAD : 0;

    /*
     * When using CLONE_PARENT, linux always sets the exit_signal for the task
     * to the exit_signal of the parent process. For our purposes, the
     * trampoline process. exit_signal has special significance for calls like
     * `wait`, so it needs to be set correctly. We add the signal part of the
     * user flags here so the ultimate child gets the right signal.
     *
     * This has the unfortunate side-effect of sending the parent two exit
     * signals. One when the true child exits, and one when the trampoline
     * exits. To work-around this we have to capture the exit signal from the
     * trampoline and supress it.
     */
    trampoline_flags |= (flags & CSIGNAL);

    sigfillset(&sigmask);
    if (sigprocmask(SIG_BLOCK, &sigmask, &info.orig_mask) != 0) {
        free(info.stack.trampoline.buffer);
        free(info.stack.process.buffer);
        completion_finish(&mgr->spawned);
        return -1;
    }

    if (clone(clone_trampoline,
              info.stack.trampoline.top, trampoline_flags, &info,
              NULL, mgr->tls_ptr, &mgr->managed_tid) < 0) {
        free(info.stack.trampoline.buffer);
        free(info.stack.process.buffer);
        completion_finish(&mgr->spawned);
        return -1;
    }

    completion_await(&info.child_ready);
    completion_finish(&mgr->spawned);

    ret = sigprocmask(SIG_SETMASK, &info.orig_mask, NULL);
    /*
     * If our final sigproc mask doesn't work, we're pretty screwed. We may
     * have started the final child now, and there's no going back. If this
     * ever happens, just crash.
     */
    assert(!ret && "sigprocmask after clone needs to succeed");

    /* If we have an error result, then set errno as needed. */
    if (info.result < 0) {
        errno = -info.result;
        return -1;
    }
    return info.result;
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

    /*
     * Backwards Compatibility: Remove once all target platforms support
     * clone_vm. Previously, we implemented vfork() via a fork() call,
     * preserve that behavior instead of failing.
     */
    if (!clone_vm_supported()) {
        if (flags & CLONE_VFORK) {
            flags &= ~(CLONE_VFORK | CLONE_VM);
        }
    }

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

    if (clone_vm_supported() && (flags & CLONE_VM)) {
        return clone_vm(flags, callback, payload);
    }

    /* !fork && !thread && !CLONE_VM. This form is unsupported. */

    errno = EINVAL;
    return -1;
}
