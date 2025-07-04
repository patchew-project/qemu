/*
 * SPDX-FileCopyrightText: Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copied from
 * https://learn.arm.com/learning-paths/cross-platform/multiplying-matrices-with-sme2/
 *
 * and modified for testing with qemu-aarch64.
 */

#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG 0

/*
 * Vanilla matrix multiplication using the by-the-book definition.
 */

void preprocess_l(uint64_t nbr, uint64_t nbc, uint64_t SVL,
                  const float *restrict a, float *restrict a_mod)
{
    // For all tiles of SVL x SVL data
    for (uint64_t By = 0; By < nbr; By += SVL) {
        for (uint64_t Bx = 0; Bx < nbc; Bx += SVL) {
            // For this tile
            const uint64_t dest = By * nbc + Bx * SVL;
            for (uint64_t j = 0; j < SVL; j++) {
                for (uint64_t i = 0; i < SVL && (Bx + i) < nbc; i++) {
                    if (By + j < nbr) {
                        a_mod[dest + i * SVL + j] = a[(By + j) * nbc + Bx + i];
                    } else {
                        // These elements are outside of matrix a, so zero them.
                        a_mod[dest + i * SVL + j] = 0.0;
                    }
                }
            }
        }
    }
}

void matmul(uint64_t M, uint64_t K, uint64_t N,
            const float *restrict matLeft, const float *restrict matRight,
            float *restrict matResult)
{
    for (uint64_t m = 0; m < M; m++) {
        for (uint64_t n = 0; n < N; n++) {
            float acc = 0.0;

            for (uint64_t k = 0; k < K; k++) {
                acc += matLeft[m * K + k] * matRight[k * N + n];
            }

            matResult[m * N + n] = acc;
        }
    }
}

/*
 * SME2 Matrix multiplication handwritten in assembly code. This is split in 2
 * functions that have to be invoked one after the other, with a top level
 * binding.
 */

/* Matrix preprocessing, in assembly. */
void preprocess_l_asm(uint64_t M, uint64_t K, const float *restrict a,
                      float *restrict a_mod);

/* Matrix multiplication (with the *transposed* RHS), in assembly. */
void matmul_asm_impl(uint64_t M, uint64_t K, uint64_t N,
                     const float *restrict matLeft_mod,
                     const float *restrict matRight, float *restrict matResult);

/* The top level matrix multiplication. */
void matmul_asm(uint64_t M, uint64_t K, uint64_t N,
                const float *restrict matLeft, const float *restrict matRight,
                float *restrict matLeft_mod, float *restrict matResult)
{
    __asm volatile("" : : :
                   "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
                   "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
                   "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
                   "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31");

    preprocess_l_asm(M, K, matLeft, matLeft_mod);
    matmul_asm_impl(M, K, N, matLeft_mod, matRight, matResult);

    __asm volatile("" : : :
                   "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
                   "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
                   "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
                   "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31");
}


// Initialize an array of float.
enum InitKind { RANDOM_INIT, LINEAR_INIT, DEAD_INIT };
void initialize_matrix(float *mat, size_t num_elements, enum InitKind kind)
{
    for (size_t i = 0; i < num_elements; i++)
        switch (kind) {
        case RANDOM_INIT:
            mat[i] = (((float)(rand() % 10000) / 100.0f) - 30.0);
            break;
        case LINEAR_INIT:
            mat[i] = i+1;
            break;
        case DEAD_INIT:
            mat[i] = nan("");
            break;
        }
}

/* Pretty print a matrix. */
void print_matrix(size_t nbr, size_t nbc, const float *mat, const char *name)
{
    printf("%s(%lu,%lu) = [", name, nbr, nbc);
    for (size_t y = 0; y < nbr; y++) {
        printf("\n  ");
        for (size_t x = 0; x < nbc; x++)
            printf("%9.2f, ", mat[y * nbc + x]);
    }
    printf("\n];\n");
}

/* Compare 2 matrices for equality. */
unsigned compare_matrices(size_t nbr, size_t nbc, const float *reference,
                          const float *result, const char *str)
{
    unsigned error = 0;

    for (size_t y = 0; y < nbr; y++) {
        for (size_t x = 0; x < nbc; x++) {
            if (fabsf(reference[y * nbc + x] - result[y * nbc + x]) >
                fabsf(0.0002f * reference[y * nbc + x])) {
                error = 1;
                if (DEBUG) {
                    printf("%lu (%lu,%lu): %f <> %f\n", y * nbc + x, x, y,
                           reference[y * nbc + x], result[y * nbc + x]);
                }
            }
        }
    }
    if (DEBUG) {
        if (error) {
            print_matrix(nbr, nbc, reference, "reference");
            print_matrix(nbr, nbc, result, "result");
        }
        printf("%s: %s !\n", str, error ? "FAILED" : "PASS");
    }

    return error;
}

uint64_t ool_svcntsw(void);

/*
 * Assumptions:
 * nbr in matLeft (M): any
 * nbc in matLeft, nbr in matRight (K): any K > 2
 * nbc in matRight (N): any
 */

int main(int argc, char **argv)
{
    /* Size parameters */
    uint64_t M, N, K;
    if (argc >= 4) {
        M = strtoul(argv[1], NULL, 0);
        K = strtoul(argv[2], NULL, 0);
        N = strtoul(argv[3], NULL, 0);
    } else {
        /* Default: 125x35x70 */
        M = 125;
        K = 35;
        N = 70;
    }

    if (DEBUG) {
        printf("\nSME2 Matrix Multiply fp32 *asm* example "
               "with args %lu %lu %lu\n", M, K, N);
    }

    const uint64_t SVL = ool_svcntsw();

    /* Calculate M of transformed matLeft.  */
    const uint64_t M_mod = SVL * (M / SVL + (M % SVL != 0 ? 1 : 0));

    float *matRight = (float *)malloc(K * N * sizeof(float));

    float *matLeft = (float *)malloc(M * K * sizeof(float));
    float *matLeft_mod = (float *)malloc(M_mod * K * sizeof(float));
    float *matLeft_mod_ref = (float *)malloc(M_mod * K * sizeof(float));

    float *matResult = (float *)malloc(M * N * sizeof(float));
    float *matResult_ref = (float *)malloc(M * N * sizeof(float));

    //  initialize_matrix(matLeft, M * K, RANDOM_INIT);
    //  initialize_matrix(matRight, K * N, RANDOM_INIT);
    initialize_matrix(matLeft, M * K, LINEAR_INIT);
    initialize_matrix(matRight, K * N, LINEAR_INIT);
    initialize_matrix(matLeft_mod, M_mod * K, DEAD_INIT);
    initialize_matrix(matResult, M * N, DEAD_INIT);

    if (DEBUG) {
        print_matrix(M, K, matLeft, "matLeft");
        print_matrix(K, N, matRight, "matRight");
    }

    matmul_asm(M, K, N, matLeft, matRight, matLeft_mod, matResult);

    /* Compute the reference values with the vanilla implementations. */
    matmul(M, K, N, matLeft, matRight, matResult_ref);
    preprocess_l(M, K, SVL, matLeft, matLeft_mod_ref);

    unsigned error = compare_matrices(K, M_mod, matLeft_mod_ref, matLeft_mod,
                                      "Matrix preprocessing");
    if (!error)
        error = compare_matrices(M, N, matResult_ref, matResult,
                                 "Matrix multiplication");

    free(matRight);

    free(matLeft);
    free(matLeft_mod);
    free(matLeft_mod_ref);

    free(matResult);
    free(matResult_ref);

    return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
