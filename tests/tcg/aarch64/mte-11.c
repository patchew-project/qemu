/*
 * Memory tagging, canonical tag checking
 *
 * Copyright (c) 2021 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mte.h"

void pass(int sig, siginfo_t *info, void *uc)
{
    assert(info->si_code == SEGV_MTESERR);
    exit(0);
}

int main(int ac, char **av)
{
    struct sigaction sa;
    int *p0, *p1, *p2;
    long excl = 1;

    /*
     * NOTE FOR REVIEWERS: to run this test locally, I modified
     * enable_mte to also activate canonical tagging checking by writing
     * to the appropriate MTX control bits. I am not sure how to modify
     * the test so that it works without that modification. Input appreciated.
     */
    enable_mte(PR_MTE_TCF_SYNC);
    p0 = alloc_mte_mem(sizeof(*p0));

    /* shouldn't fault on a canonical ptr */
    *p0 = 32;

    /* decanonicalize ptr */
    p0 = (int *) (((long) p0) | (1ll << 56));

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pass;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);

    /* should fault on our modified ptr */
    *p0 = 64;

    abort();
}
