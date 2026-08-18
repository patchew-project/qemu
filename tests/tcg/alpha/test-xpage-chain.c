/*
 * Cross-page TB chaining hazard test.
 *
 * Phase 1: a direct branch (br) near the end of page A targets page B.
 *          Run it enough times that QEMU chains TB_A -> TB_B.
 * Phase 2: mprotect page B away. Re-running must fault.
 * Phase 3: remap page B with different code. Re-running must execute the
 *          NEW code, not a stale chained translation of the old code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#define PS 8192

static sigjmp_buf jb;
/*
 * Written by the SIGSEGV handler and read by main(), so it must not be
 * cached in a register across the faulting call.
 */
static volatile sig_atomic_t caught;

static void segv(int sig)
{
    caught = 1;
    siglongjmp(jb, 1);
}

/* lda $0, imm($31)  -> v0 = imm */
static unsigned int lda_v0(int imm)
{
    return 0x201F0000u | (unsigned short)imm;
}

int main(void)
{
    struct sigaction sa;
    int rc = 0;
    unsigned char *m = mmap(NULL, 2 * PS, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        perror("mmap");
        return 2;
    }

    unsigned char *pa = m, *pb = m + PS;
    unsigned int *entry = (unsigned int *)(pa + PS - 64);
    unsigned int *tgt = (unsigned int *)(pb + 16);

    entry[0] = lda_v0(1);
    long disp = ((long)tgt - ((long)&entry[1] + 4)) / 4;
    entry[1] = 0xC3E00000u | (unsigned int)(disp & 0x1FFFFF);  /* br $31,tgt */
    tgt[0] = 0x6BFA8001u;                                      /* ret        */
    __builtin___clear_cache((char *)m, (char *)m + 2 * PS);

    long (*fn)(void) = (long (*)(void))entry;

    for (int i = 0; i < 200000; i++) {
        if (fn() != 1) {
            printf("FAIL: phase 1 wrong result\n");
            return 1;
        }
    }
    printf("phase 1 ok (chained)\n");

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = segv;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        perror("sigaction");
        return 2;
    }
    if (mprotect(pb, PS, PROT_NONE) != 0) {
        perror("mprotect");
        return 2;
    }
    if (sigsetjmp(jb, 1) == 0) {
        fn();
        printf("FAIL: phase 2 executed page B after mprotect(PROT_NONE)\n");
        rc = 1;
    } else if (!caught) {
        printf("FAIL: phase 2 longjmp without entering the handler\n");
        rc = 1;
    } else {
        printf("phase 2 ok (faulted)\n");
    }

    /* Phase 3: remap with different code, expect the new code to run. */
    if (mprotect(pb, PS, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        perror("mprotect back");
        return 2;
    }
    tgt[0] = lda_v0(2);
    tgt[1] = 0x6BFA8001u;
    __builtin___clear_cache((char *)pb, (char *)pb + PS);

    long r = fn();
    if (r != 2) {
        printf("FAIL: phase 3 returned %ld, expected 2 (stale chain)\n", r);
        rc = 1;
    } else {
        printf("phase 3 ok (new code ran)\n");
    }
    return rc;
}
