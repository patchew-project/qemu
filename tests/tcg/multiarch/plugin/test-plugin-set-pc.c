/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026, Florian Hofhammer <florian.hofhammer@epfl.ch>
 *
 * This test set exercises the qemu_plugin_set_pc() function in four different
 * contexts:
 * 1. in a syscall callback,
 * 2. in an instruction callback during normal execution,
 * 3. in an instruction callback during signal handling,
 * 4. in a memory access callback.
 * Note: using the volatile guards is necessary to prevent the compiler from
 * doing dead code elimination even on -O0, which would cause everything after
 * the asserts and thus also the target labels to be optimized away.
 */
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <setjmp.h>

#define NOINLINE __attribute__((noinline))
#define NORETURN __attribute__((noreturn))

static int signal_handled;
/*
 * The volatile variable is used as a guard to prevent the compiler from
 * optimizing away "unreachable" labels.
 */
static volatile uint32_t guard = 1;

/*
 * This test executes a magic syscall which communicates two addresses to the
 * plugin via the syscall arguments. Whenever we reach the "bad" instruction
 * during normal execution, the plugin should redirect control flow to the
 * "good" instruction instead.
 */
NOINLINE void test_insn(void)
{
    long ret = syscall(4095, &&bad_insn, &&good_insn, NULL);
    assert(ret == 0 && "Syscall filter did not return expected value");
    if (guard) {
bad_insn:
        assert(0 && "PC redirection in instruction callback failed");
    } else {
good_insn:
        return;
    }
}

/*
 * This signal handler communicates a "bad" and a "good" address to the plugin
 * similar to the previous test, and skips to the "good" address when the "bad"
 * one is reached. This serves to test whether PC redirection via
 * qemu_plugin_set_pc() also works properly in a signal handler context.
 */
NOINLINE void usr1_handler(int signum)
{
    long ret = syscall(4095, &&bad_signal, &&good_signal, NULL);
    assert(ret == 0 && "Syscall filter did not return expected value");
    if (guard) {
bad_signal:
        assert(0 && "PC redirection in instruction callback failed");
    } else {
good_signal:
        signal_handled = 1;
        return;
    }
}

/*
 * This test sends a signal to the process, which should trigger the above
 * signal handler. The signal handler should then exercise the PC redirection
 * functionality in the context of a signal handler, which behaves a bit
 * differently from normal execution.
 */
NOINLINE void test_sighandler(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = usr1_handler;
    sigaction(SIGUSR1, &sa, NULL);
    pid_t pid = getpid();
    kill(pid, SIGUSR1);
    assert(signal_handled == 1 && "Signal handler was not executed properly");
}

/*
 * This test communicates a "good" address and the address of a local variable
 * to the plugin. Upon accessing the local variable, the plugin should then
 * redirect control flow to the "good" address via qemu_plugin_set_pc().
 */
NOINLINE void test_mem(void)
{
    long ret = syscall(4095, NULL, &&good_mem, &guard);
    assert(ret == 0 && "Syscall filter did not return expected value");
    if (guard) {
        assert(0 && "PC redirection in memory access callback failed");
    } else {
good_mem:
        return;
    }
}

/*
 * This test executes a magic syscall which is intercepted and its actual
 * execution skipped via the qemu_plugin_set_pc() API. In a proper plugin,
 * syscall skipping would rather be implemented via the syscall filtering
 * callback, but we want to make sure qemu_plugin_set_pc() works in different
 * contexts.
 */
NOINLINE NORETURN
void test_syscall(void)
{
    syscall(4096, &&good_syscall);
    if (guard) {
        assert(0 && "PC redirection in syscall callback failed");
    } else {
good_syscall:
        /*
         * Note: we execute this test last and exit straight from here because
         * when the plugin redirects control flow upon syscall, the stack frame
         * for the syscall function (and potential other functions in the call
         * chain in libc) is still live and the stack is not unwound properly.
         * Thus, returning from here is risky and breaks on some architectures,
         * so we just exit directly from this test.
         */
        _exit(EXIT_SUCCESS);
    }
}


int main(int argc, char *argv[])
{
    test_insn();
    test_sighandler();
    test_mem();
    test_syscall();
}
