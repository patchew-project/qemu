/*
 *  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

int err;

#include "hex_test.h"

static inline int32_t atomic_inc32(int32_t *x)
{
    int32_t old, dummy;
    __asm__ __volatile__(
        "1: %0 = memw_locked(%2)\n\t"
        "   %1 = add(%0, #1)\n\t"
        "   memw_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

static inline int64_t atomic_inc64(int64_t *x)
{
    int64_t old, dummy;
    __asm__ __volatile__(
        "1: %0 = memd_locked(%2)\n\t"
        "   %1 = #1\n\t"
        "   %1 = add(%0, %1)\n\t"
        "   memd_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

static inline int32_t atomic_dec32(int32_t *x)
{
    int32_t old, dummy;
    __asm__ __volatile__(
        "1: %0 = memw_locked(%2)\n\t"
        "   %1 = add(%0, #-1)\n\t"
        "   memw_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

static inline int64_t atomic_dec64(int64_t *x)
{
    int64_t old, dummy;
    __asm__ __volatile__(
        "1: %0 = memd_locked(%2)\n\t"
        "   %1 = #-1\n\t"
        "   %1 = add(%0, %1)\n\t"
        "   memd_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

#define LOOP_CNT 1000
volatile int32_t tick32 = 1; /* Using volatile because we are testing atomics */
volatile int64_t tick64 = 1; /* Using volatile because we are testing atomics */

void *thread1_func(void *arg)
{
    for (int i = 0; i < LOOP_CNT; i++) {
        atomic_inc32(&tick32);
        atomic_dec64(&tick64);
    }
    return NULL;
}

void *thread2_func(void *arg)
{
    for (int i = 0; i < LOOP_CNT; i++) {
        atomic_dec32(&tick32);
        atomic_inc64(&tick64);
    }
    return NULL;
}

void test_pthread(void)
{
    pthread_t tid1, tid2;

    pthread_create(&tid1, NULL, thread1_func, "hello1");
    pthread_create(&tid2, NULL, thread2_func, "hello2");
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    check32(tick32, 1);
    check64(tick64, 1);
}

int main(int argc, char **argv)
{
    test_pthread();
    puts(err ? "FAIL" : "PASS");
    return err;
}
