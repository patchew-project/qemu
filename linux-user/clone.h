#ifndef CLONE_H
#define CLONE_H

/*
 * qemu_clone executes the given `callback`, with the given payload as the
 * first argument, in a new process created with the given flags. Some clone
 * flags, such as *SETTLS, *CLEARTID are not supported. The child thread ID is
 * returned on success, otherwise negative errno is returned on clone failure.
 */
int qemu_clone(int flags, int (*callback)(void *), void *payload);

/* Returns true if the given clone flags can be emulated with libc fork. */
static bool clone_flags_are_fork(unsigned int flags)
{
    return flags == SIGCHLD;
}

/* Returns true if the given clone flags can be emulated with pthread_create. */
static bool clone_flags_are_thread(unsigned int flags)
{
    return flags == (
        CLONE_VM | CLONE_FS | CLONE_FILES |
        CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM
    );
}

#endif /* CLONE_H */
