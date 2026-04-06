/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define INPUT_SIZE (8 * 1024 * 1024)

static void fill_input(Bytef *data, size_t len)
{
    static const unsigned char pattern[] =
        "QEMU passthrough syscall-filter demo payload\n";
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
    Bytef *input = NULL;
    Bytef *compressed = NULL;
    Bytef *output = NULL;
    uLongf compressed_len;
    uLongf output_len;
    uLongf compressed_bound;
    int ret = EXIT_FAILURE;

    input = malloc(INPUT_SIZE);
    if (input == NULL) {
        perror("malloc");
        goto cleanup;
    }

    fill_input(input, INPUT_SIZE);

    compressed_bound = compressBound(INPUT_SIZE);
    if (compressed_bound == 0) {
        fprintf(stderr, "compressBound failed\n");
        goto cleanup;
    }

    compressed = malloc(compressed_bound);
    output = malloc(INPUT_SIZE);
    if (compressed == NULL || output == NULL) {
        perror("malloc");
        goto cleanup;
    }

    compressed_len = compressed_bound;
    if (compress2(compressed, &compressed_len, input, INPUT_SIZE,
                  Z_BEST_COMPRESSION) != Z_OK) {
        fprintf(stderr, "compress2 failed\n");
        goto cleanup;
    }

    output_len = INPUT_SIZE;
    if (uncompress(output, &output_len, compressed, compressed_len) != Z_OK) {
        fprintf(stderr, "uncompress failed\n");
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

    printf("passthrough demo compressed %u bytes to %lu bytes\n",
           INPUT_SIZE, (unsigned long)compressed_len);
    puts("passthrough demo round-tripped successfully");
    ret = EXIT_SUCCESS;

cleanup:
    free(output);
    free(compressed);
    free(input);
    return ret;
}
