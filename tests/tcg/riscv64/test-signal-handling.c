/*
 * Test for linux-user signal handling.
 *
 * This ensures that integer and fp register values are
 * saved as expected in the sigcontext, created by a SIGILL.
 *
 * TODO: Register restore is not explicitly verified, except
 * for advancing pc, and the restoring of registers that were
 * clobbered by the compiler in the signal handler.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <execinfo.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <asm/sigcontext.h>

/*
 * This horrible hack seems to be required when including
 * signal.h and asm/sigcontext.h, to prevent sigcontext
 * redefinition by bits/sigcontext.h :(
 *
 * bits/sigcontext.h does not have the extended state or
 * RISCV_V_MAGIC, etc. It could have just been introduced
 * as a new type.
 */
#define _BITS_SIGCONTEXT_H 1
#include <signal.h>

static uint64_t *initial_gvalues;
static uint64_t *final_gvalues;
static uint64_t *signal_gvalues;
static double *initial_fvalues;
static double *final_fvalues;
static double *signal_fvalues;

extern unsigned long unimp_addr[];

static bool got_signal = false;

#define BT_BUF_SIZE 100

static void *find_callchain_root(void)
{
   int nptrs;
   void *buffer[BT_BUF_SIZE];

   nptrs = backtrace(buffer, BT_BUF_SIZE);

   return buffer[nptrs - 1];
}

static void *callchain_root;

static void ILL_handler(int signo, siginfo_t *info, void *context)
{
    ucontext_t *uc = context;
    struct sigcontext *sc = (struct sigcontext *)&uc->uc_mcontext;

    got_signal = true;

    assert(unimp_addr == info->si_addr);
    assert(sc->sc_regs.pc == (unsigned long)info->si_addr);

    /* Ensure stack unwind through the signal frame is not broken */
    assert(callchain_root == find_callchain_root());

    for (int i = 0; i < 31; i++) {
        ((uint64_t *)signal_gvalues)[i] = ((unsigned long *)&sc->sc_regs.ra)[i];
    }

    for (int i = 0; i < 32; i++) {
        ((uint64_t *)signal_fvalues)[i] = sc->sc_fpregs.d.f[i];
    }
    /* Test sc->sc_fpregs.d.fcsr ? */

    sc->sc_regs.pc += 4;
}

static void init_test(void)
{
    int i;

    callchain_root = find_callchain_root();

    initial_gvalues = malloc(8 * 31);
    memset(initial_gvalues, 0, 8 * 31);
    final_gvalues = malloc(8 * 31);
    memset(final_gvalues, 0, 8 * 31);
    signal_gvalues = malloc(8 * 31);
    memset(signal_gvalues, 0, 8 * 31);

    initial_fvalues = malloc(8 * 32);
    memset(initial_fvalues, 0, 8 * 32);
    for (i = 0; i < 32 ; i++) {
        initial_fvalues[i] = 3.142 * (i + 1);
    }
    final_fvalues = malloc(8 * 32);
    memset(final_fvalues, 0, 8 * 32);
    signal_fvalues = malloc(8 * 32);
    memset(signal_fvalues, 0, 8 * 32);
}

static void run_test(void)
{
    asm volatile(
    /* Save initial values from gp registers */
"    mv    t0, %[initial_gvalues]    \n"
"    sd    x1, 0x0(t0)               \n"
"    sd    x2, 0x8(t0)               \n"
"    sd    x3, 0x10(t0)              \n"
"    sd    x4, 0x18(t0)              \n"
"    sd    x5, 0x20(t0)              \n"
"    sd    x6, 0x28(t0)              \n"
"    sd    x7, 0x30(t0)              \n"
"    sd    x8, 0x38(t0)              \n"
"    sd    x9, 0x40(t0)              \n"
"    sd    x10, 0x48(t0)             \n"
"    sd    x11, 0x50(t0)             \n"
"    sd    x12, 0x58(t0)             \n"
"    sd    x13, 0x60(t0)             \n"
"    sd    x14, 0x68(t0)             \n"
"    sd    x15, 0x70(t0)             \n"
"    sd    x16, 0x78(t0)             \n"
"    sd    x17, 0x80(t0)             \n"
"    sd    x18, 0x88(t0)             \n"
"    sd    x19, 0x90(t0)             \n"
"    sd    x20, 0x98(t0)             \n"
"    sd    x21, 0xa0(t0)             \n"
"    sd    x22, 0xa8(t0)             \n"
"    sd    x23, 0xb0(t0)             \n"
"    sd    x24, 0xb8(t0)             \n"
"    sd    x25, 0xc0(t0)             \n"
"    sd    x26, 0xc8(t0)             \n"
"    sd    x27, 0xd0(t0)             \n"
"    sd    x28, 0xd8(t0)             \n"
"    sd    x29, 0xe0(t0)             \n"
"    sd    x30, 0xe8(t0)             \n"
"    sd    x31, 0xf0(t0)             \n"
    /* Load initial values into float registers */
"    mv    t0, %[initial_fvalues]    \n"
"    fld    f0, 0x0(t0)              \n"
"    fld    f1, 0x8(t0)              \n"
"    fld    f2, 0x10(t0)             \n"
"    fld    f3, 0x18(t0)             \n"
"    fld    f4, 0x20(t0)             \n"
"    fld    f5, 0x28(t0)             \n"
"    fld    f6, 0x30(t0)             \n"
"    fld    f7, 0x38(t0)             \n"
"    fld    f8, 0x40(t0)             \n"
"    fld    f9, 0x48(t0)             \n"
"    fld    f10, 0x50(t0)            \n"
"    fld    f11, 0x58(t0)            \n"
"    fld    f12, 0x60(t0)            \n"
"    fld    f13, 0x68(t0)            \n"
"    fld    f14, 0x70(t0)            \n"
"    fld    f15, 0x78(t0)            \n"
"    fld    f16, 0x80(t0)            \n"
"    fld    f17, 0x88(t0)            \n"
"    fld    f18, 0x90(t0)            \n"
"    fld    f19, 0x98(t0)            \n"
"    fld    f20, 0xa0(t0)            \n"
"    fld    f21, 0xa8(t0)            \n"
"    fld    f22, 0xb0(t0)            \n"
"    fld    f23, 0xb8(t0)            \n"
"    fld    f24, 0xc0(t0)            \n"
"    fld    f25, 0xc8(t0)            \n"
"    fld    f26, 0xd0(t0)            \n"
"    fld    f27, 0xd8(t0)            \n"
"    fld    f28, 0xe0(t0)            \n"
"    fld    f29, 0xe8(t0)            \n"
"    fld    f30, 0xf0(t0)            \n"
"    fld    f31, 0xf8(t0)            \n"
    /* Trigger the SIGILL */
".global unimp_addr                  \n"
"unimp_addr:                         \n"
"    unimp                           \n"
"    nop                             \n"
    /* Save final values from gp registers */
"    mv    t0, %[final_gvalues]      \n"
"    sd    x1, 0x0(t0)               \n"
"    sd    x2, 0x8(t0)               \n"
"    sd    x3, 0x10(t0)              \n"
"    sd    x4, 0x18(t0)              \n"
"    sd    x5, 0x20(t0)              \n"
"    sd    x6, 0x28(t0)              \n"
"    sd    x7, 0x30(t0)              \n"
"    sd    x8, 0x38(t0)              \n"
"    sd    x9, 0x40(t0)              \n"
"    sd    x10, 0x48(t0)             \n"
"    sd    x11, 0x50(t0)             \n"
"    sd    x12, 0x58(t0)             \n"
"    sd    x13, 0x60(t0)             \n"
"    sd    x14, 0x68(t0)             \n"
"    sd    x15, 0x70(t0)             \n"
"    sd    x16, 0x78(t0)             \n"
"    sd    x17, 0x80(t0)             \n"
"    sd    x18, 0x88(t0)             \n"
"    sd    x19, 0x90(t0)             \n"
"    sd    x20, 0x98(t0)             \n"
"    sd    x21, 0xa0(t0)             \n"
"    sd    x22, 0xa8(t0)             \n"
"    sd    x23, 0xb0(t0)             \n"
"    sd    x24, 0xb8(t0)             \n"
"    sd    x25, 0xc0(t0)             \n"
"    sd    x26, 0xc8(t0)             \n"
"    sd    x27, 0xd0(t0)             \n"
"    sd    x28, 0xd8(t0)             \n"
"    sd    x29, 0xe0(t0)             \n"
"    sd    x30, 0xe8(t0)             \n"
"    sd    x31, 0xf0(t0)             \n"
    /* Save final values from float registers */
"    mv    t0, %[final_fvalues]      \n"
"    fsd    f0, 0x0(t0)              \n"
"    fsd    f1, 0x8(t0)              \n"
"    fsd    f2, 0x10(t0)             \n"
"    fsd    f3, 0x18(t0)             \n"
"    fsd    f4, 0x20(t0)             \n"
"    fsd    f5, 0x28(t0)             \n"
"    fsd    f6, 0x30(t0)             \n"
"    fsd    f7, 0x38(t0)             \n"
"    fsd    f8, 0x40(t0)             \n"
"    fsd    f9, 0x48(t0)             \n"
"    fsd    f10, 0x50(t0)            \n"
"    fsd    f11, 0x58(t0)            \n"
"    fsd    f12, 0x60(t0)            \n"
"    fsd    f13, 0x68(t0)            \n"
"    fsd    f14, 0x70(t0)            \n"
"    fsd    f15, 0x78(t0)            \n"
"    fsd    f16, 0x80(t0)            \n"
"    fsd    f17, 0x88(t0)            \n"
"    fsd    f18, 0x90(t0)            \n"
"    fsd    f19, 0x98(t0)            \n"
"    fsd    f20, 0xa0(t0)            \n"
"    fsd    f21, 0xa8(t0)            \n"
"    fsd    f22, 0xb0(t0)            \n"
"    fsd    f23, 0xb8(t0)            \n"
"    fsd    f24, 0xc0(t0)            \n"
"    fsd    f25, 0xc8(t0)            \n"
"    fsd    f26, 0xd0(t0)            \n"
"    fsd    f27, 0xd8(t0)            \n"
"    fsd    f28, 0xe0(t0)            \n"
"    fsd    f29, 0xe8(t0)            \n"
"    fsd    f30, 0xf0(t0)            \n"
"    fsd    f31, 0xf8(t0)            \n"
    : "=m" (initial_gvalues),
      "=m" (final_gvalues),
      "=m" (final_fvalues)
    : "m" (initial_fvalues),
      [initial_gvalues] "r" (initial_gvalues),
      [initial_fvalues] "r" (initial_fvalues),
      [final_gvalues] "r" (final_gvalues),
      [final_fvalues] "r" (final_fvalues)
    : "t0",
      "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
      "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
      "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
      "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31");

    assert(got_signal);

    /*
     * x4 / t0 is used in the asm so it has to be handled specially
     * and is not a simple equality.
     */
    assert(initial_gvalues[4] == (unsigned long)initial_gvalues);
    assert(signal_gvalues[4] == (unsigned long)initial_fvalues);
    assert(final_gvalues[4] == (unsigned long)final_gvalues);
    initial_gvalues[4] = final_gvalues[4] = signal_gvalues[4] = 0;

    /*
     * Ensure registers match before, inside, and after signal
     * handler.
     */
    assert(!memcmp(initial_gvalues, final_gvalues, 8 * 31));
    assert(!memcmp(initial_gvalues, signal_gvalues, 8 * 31));
    assert(!memcmp(initial_fvalues, final_fvalues, 8 * 32));
    assert(!memcmp(initial_fvalues, signal_fvalues, 8 * 32));
}

int main(void)
{
    struct sigaction act = { 0 };

    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = &ILL_handler;
    if (sigaction(SIGILL, &act, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    init_test();

    run_test();
}
