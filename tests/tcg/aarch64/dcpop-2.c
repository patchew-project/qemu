/* Test execution of DC CVAP instruction on unmapped address */

#include <asm/hwcap.h>
#include <sys/auxv.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef HWCAP_DCPOP
#define HWCAP_DCPOP (1 << 16)
#endif

static void signal_handler(int sig)
{
    exit(EXIT_SUCCESS);
}

static int do_dc_cvap(void)
{
    struct sigaction sa = {
        .sa_handler = signal_handler,
    };

    if (sigaction(SIGILL, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    asm volatile("dc cvap, %0\n\t" :: "r"(NULL));

    return EXIT_FAILURE;
}

int main(void)
{
    if (getauxval(AT_HWCAP) & HWCAP_DCPOP) {
        return do_dc_cvap();
    } else {
        printf("SKIP: no HWCAP_DCPOP on this system\n");
        return EXIT_SUCCESS;
    }
}
