/* Test execution of DC CVADP instruction */

#include <asm/hwcap.h>
#include <sys/auxv.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef HWCAP2_DCPODP
#define HWCAP2_DCPODP (1 << 0)
#endif

static void sigill_handler(int sig)
{
    exit(EXIT_FAILURE);
}

static int do_dc_cvadp(void)
{
    struct sigaction sa = {
        .sa_handler = sigill_handler,
    };

    if (sigaction(SIGILL, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    asm volatile("dc cvadp, %0\n\t" :: "r"(&sa));

    return 0;
}

int main(void)
{
    if (getauxval(AT_HWCAP) & HWCAP2_DCPODP) {
        return do_dc_cvadp();
    } else {
        printf("SKIP: no HWCAP2_DCPODP on this system\n");
        return 0;
    }

    return 0;
}
