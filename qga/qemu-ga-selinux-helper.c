/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <glib.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        return EXIT_FAILURE;
    }

    execvp(argv[1], argv + 1);

    int err = errno;
    fprintf(stderr, "%s: %s\n", argv[1], strerror(err));

    exit(EXIT_FAILURE);
}
