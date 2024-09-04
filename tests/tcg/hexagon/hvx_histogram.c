/*
 *  Copyright(c) 2021-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hvx_histogram_row.h"

const int vector_len = 128;
const int width = 275;
const int height = 20;
const int stride = (width + vector_len - 1) & -vector_len;

int err;

static uint8_t input[height][stride] __attribute__((aligned(128))) = {
#include "hvx_histogram_input.h"
};

static int result[256] __attribute__((aligned(128)));
static int expect[256] __attribute__((aligned(128)));

static void check(void)
{
    for (int i = 0; i < 256; i++) {
        int res = result[i];
        int exp = expect[i];
        if (res != exp) {
            printf("ERROR at %3d: 0x%04x != 0x%04x\n",
                   i, res, exp);
            err++;
        }
    }
}

static void ref_histogram(uint8_t *src, int stride, int width, int height,
                          int *hist)
{
    for (int i = 0; i < 256; i++) {
        hist[i] = 0;
    }

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            hist[src[i * stride + j]]++;
        }
    }
}

static void hvx_histogram(uint8_t *src, int stride, int width, int height,
                          int *hist)
{
    int n = 8192 / width;

    for (int i = 0; i < 256; i++) {
        hist[i] = 0;
    }

    for (int i = 0; i < height; i += n) {
        int k = height - i > n ? n : height - i;
        hvx_histogram_row(src, stride, width, k, hist);
        src += n * stride;
    }
}

int main()
{
    ref_histogram(&input[0][0], stride, width, height, expect);
    hvx_histogram(&input[0][0], stride, width, height, result);
    check();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
