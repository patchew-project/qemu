/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test attempts to execute an invalid syscall. The syscall test plugin
 * should intercept this.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void exit_success(void) __attribute__((section(".redirect"), noinline,
                                       noreturn, used));

void exit_success(void) {
    _exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    long ret = syscall(0xc0deUL);
    if (ret != 0L) {
        perror("");
    }
    /* We should never get here */
    return EXIT_FAILURE;
}
