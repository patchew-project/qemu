/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test attempts to execute a magic syscall. The syscall test plugin
 * should intercept this and returns an expected value.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    long ret = syscall(0x66CCFF);
    if (ret != 0xFFCC66) {
        perror("ERROR: syscall returned unexpected value!!!");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
