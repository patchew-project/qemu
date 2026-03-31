/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zcompress-thunk.h"

#define INPUT_SIZE (8 * 1024 * 1024)
#define Z_BEST_COMPRESSION 9

static void fill_input(unsigned char *data, size_t len)
{
    static const unsigned char pattern[] =
        "QEMU syscall filter zlib compression demo payload\n";
    size_t i;

    for (i = 0; i < len; i++) {
        data[i] = pattern[i % (sizeof(pattern) - 1)];
        if ((i % 4096) == 0) {
            data[i] = (unsigned char)(i >> 12);
        }
    }
}

int main(void)
{
    unsigned char *input = NULL;
    unsigned char *compressed = NULL;
    unsigned char *output = NULL;
    size_t compressed_len;
    size_t output_len;
    size_t compressed_bound;
    int ret = EXIT_FAILURE;

    input = malloc(INPUT_SIZE);
    if (input == NULL) {
        perror("malloc");
        goto cleanup;
    }

    fill_input(input, INPUT_SIZE);

    compressed_bound = zcompress_compress_bound(INPUT_SIZE);
    if (compressed_bound == 0) {
        fprintf(stderr, "zcompress_compress_bound failed\n");
        goto cleanup;
    }

    compressed = malloc(compressed_bound);
    output = malloc(INPUT_SIZE);
    if (compressed == NULL || output == NULL) {
        perror("malloc");
        goto cleanup;
    }

    compressed_len = compressed_bound;
    if (zcompress_compress(input, INPUT_SIZE, compressed, &compressed_len,
                           Z_BEST_COMPRESSION) != 0) {
        fprintf(stderr, "zcompress_compress failed\n");
        goto cleanup;
    }

    output_len = INPUT_SIZE;
    if (zcompress_uncompress(compressed, compressed_len, output,
                             &output_len) != 0) {
        fprintf(stderr, "zcompress_uncompress failed\n");
        goto cleanup;
    }

    if (output_len != INPUT_SIZE || memcmp(input, output, INPUT_SIZE) != 0) {
        fprintf(stderr, "round-trip mismatch\n");
        goto cleanup;
    }

    if (compressed_len >= INPUT_SIZE) {
        fprintf(stderr, "compressed output was not smaller than input\n");
        goto cleanup;
    }

    printf("zlib demo compressed %u bytes to %zu bytes\n",
           INPUT_SIZE, compressed_len);
    puts("zlib demo round-tripped successfully");
    ret = EXIT_SUCCESS;

cleanup:
    free(output);
    free(compressed);
    free(input);
    return ret;
}
