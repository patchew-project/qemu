/*
 *  Source file of a benchmark program involving calculations of
 *  a product of two matrixes nxn whose elements are "int32_t". The
 *  number n can be given via command line, and the default is 200.
 *
 *  This file is a part of the project "TCG Continuous Benchmarking".
 *
 *  Copyright (C) 2020  Ahmed Karaman <ahmedkhaledkaraman@gmail.com>
 *  Copyright (C) 2020  Aleksandar Markovic <aleksandar.qemu.devel@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Number of columns and rows in all matrixes*/
#define DEFAULT_MATRIX_SIZE     200
#define MIN_MATRIX_SIZE         2
#define MAX_MATRIX_SIZE         200000

void main(int argc, char *argv[])
{
    int32_t **matrix_a;
    int32_t **matrix_b;
    int32_t **matrix_res;
    size_t i;
    size_t j;
    size_t k;
    int32_t matrix_size = DEFAULT_MATRIX_SIZE;
    int32_t option;

    /* Parse command line options */
    while ((option = getopt(argc, argv, "n:")) != -1) {
        if (option == 'n') {
            int32_t user_matrix_size = atoi(optarg);

            /* Check if the value is a string or zero */
            if (user_matrix_size == 0) {
                fprintf(stderr, "Error ... Invalid value for option '-n'.\n");
                exit(EXIT_FAILURE);
            }
            /* Check if the value is a negative number */
            if (user_matrix_size < MIN_MATRIX_SIZE) {
                fprintf(stderr, "Error ... Value for option '-n' cannot be a "
                                "number less than %d.\n", MIN_MATRIX_SIZE);
                exit(EXIT_FAILURE);
            }
            /* Check if the value is too large */
            if (user_matrix_size > MAX_MATRIX_SIZE) {
                fprintf(stderr, "Error ... Value for option '-n' cannot be "
                                "more than %d.\n", MAX_MATRIX_SIZE);
                exit(EXIT_FAILURE);
            }
            matrix_size = user_matrix_size;
        } else {
            exit(EXIT_FAILURE);
        }
    }

    /* Allocate the memory space for all matrixes */
    matrix_a = (int32_t **)malloc(matrix_size * sizeof(int32_t *));
    for (i = 0; i < matrix_size; i++) {
        matrix_a[i] = (int32_t *)malloc(matrix_size * sizeof(int32_t));
    }
    matrix_b = (int32_t **)malloc(matrix_size * sizeof(int32_t *));
    for (i = 0; i < matrix_size; i++) {
        matrix_b[i] = (int32_t *)malloc(matrix_size * sizeof(int32_t));
    }
    matrix_res = (int32_t **)malloc(matrix_size * sizeof(int32_t *));
    for (i = 0; i < matrix_size; i++) {
        matrix_res[i] = (int32_t *)malloc(matrix_size * sizeof(int32_t));
    }

    /* Populate matrix_a and matrix_b with random numbers */
    srand(1);
    for (i = 0; i < matrix_size; i++) {
        for (j = 0; j < matrix_size; j++) {
            matrix_a[i][j] = (rand()) / (RAND_MAX / 100);
            matrix_b[i][j] = (rand()) / (RAND_MAX / 100);
        }
    }

    /* Calculate the product of two matrixes */
    for (i = 0; i < matrix_size; i++) {
        for (j = 0; j < matrix_size; j++) {
            matrix_res[i][j] = 0;
            for (k = 0; k < matrix_size; k++) {
                matrix_res[i][j] += matrix_a[i][k] * matrix_b[k][j];
            }
        }
    }

    /* Control printing */
    printf("CONTROL RESULT:\n");
    printf(" %d %d\n", matrix_res[0][0], matrix_res[0][1]);
    printf(" %d %d\n", matrix_res[1][0], matrix_res[1][1]);

    /* Free all previously allocated space */
    for (i = 0; i < matrix_size; i++) {
        free(matrix_a[i]);
        free(matrix_b[i]);
        free(matrix_res[i]);
    }
    free(matrix_a);
    free(matrix_b);
    free(matrix_res);
}
