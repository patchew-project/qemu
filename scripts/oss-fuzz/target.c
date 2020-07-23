/*
 * Copyright Red Hat Inc., 2020
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>


/* Required for oss-fuzz to consider the binary a target. */
static const char *magic __attribute__((used)) = "LLVMFuzzerTestOneInput";
static const char args[] = {QEMU_FUZZ_ARGS, 0x00};
static const char objects[] = {QEMU_FUZZ_OBJECTS, 0x00};

int main(int argc, char *argv[])
{
    char path[PATH_MAX] = {0};
    char *dir = dirname(argv[0]);
    strncpy(path, dir, PATH_MAX);
    strcat(path, "/deps/qemu-fuzz-i386-target-general-fuzz");

    setenv("QEMU_FUZZ_ARGS", args, 0);
    setenv("QEMU_FUZZ_OBJECTS", objects, 0);

    argv[0] = path;
    int ret = execvp(path, argv);
    if (ret) {
        perror("execv");
    }
    return ret;
}
