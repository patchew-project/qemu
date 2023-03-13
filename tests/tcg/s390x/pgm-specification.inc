/*
 * Common code for specification exception testing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifdef CONFIG_SOFTMMU
    .org 0x8d
ilc:
    .org 0x8e
program_interruption_code:
    .org 0x150
program_old_psw:
    .org 0x1D0                         /* program new PSW */
    .quad 0x180000000,pgm              /* 64-bit mode */
    .org 0x200                         /* lowcore padding */

    .globl _start
_start:
    lpswe start64_psw
start64:
    CODE
    j failure

pgm:
    chhsi program_interruption_code,0x6          /* PGM_SPECIFICATION? */
    jne failure
    lg %r0,expected_old_psw+8                    /* ilc adjustment */
    llgc %r1,ilc
    agr %r0,%r1
    stg %r0,expected_old_psw+8
    clc expected_old_psw(16),program_old_psw     /* correct location? */
    jne failure
    lpswe success_psw
failure:
    lpswe failure_psw

    .align 8
start64_psw:
    .quad 0x180000000,start64          /* 64-bit mode */
expected_old_psw:
#ifndef EXPECTED_OLD_PSWA
#define EXPECTED_OLD_PSWA expected_old_pswa
#endif
    .quad 0x180000000,EXPECTED_OLD_PSWA
success_psw:
    .quad 0x2000180000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000180000000,0            /* disabled wait */
    DATA
#else
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef EXPECTED_OLD_PSWA
extern char expected_old_pswa[];
#define EXPECTED_OLD_PSWA (long)expected_old_pswa
#endif

static void handle_sigill(int sig, siginfo_t *info, void *ucontext)
{
    if ((long)info->si_addr != EXPECTED_OLD_PSWA) {
        _exit(EXIT_FAILURE);
    }
    _exit(EXIT_SUCCESS);
}

asm("    .data\n"
    "    .align 8\n"
    DATA
    "    .previous\n");

int main(void)
{
    struct sigaction act;
    int err;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    err = sigaction(SIGILL, &act, NULL);
    assert(err == 0);

    asm volatile(CODE);

    return EXIT_FAILURE;
}
#endif
