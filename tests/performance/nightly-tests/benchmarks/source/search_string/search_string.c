/*
 *  Source file of a benchmark program that searches for the occurrence
 *  of a small string in a much larger random string ("needle in a hay").
 *  That searching is repeated a number of times (default is 20 times),
 *  and each time a different large random string ("hay") is generated.
 *  The number of repetitions can be set via command line.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* Length of a long string to be searched (including terminating zero) */
#define HAYSTACK_LEN                  30000

/* Number of repetitions to be performed each with different input */
#define DEFAULT_REPETITION_COUNT      100
#define MIN_REPETITION_COUNT          1
#define MAX_REPETITION_COUNT          10000


/* Generate a random string of given length and containing only small letters */
static void gen_random_string(char *s, const int len)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz";

    for (size_t i = 0; i < (len - 1); i++) {
        s[i] = letters[rand() % (sizeof(letters) - 1)];
    }

    s[len - 1] = 0;
}

void main(int argc, char *argv[])
{
    char haystack[HAYSTACK_LEN];
    const char needle[] = "aaa ";
    char *found_needle;
    int32_t found_cnt = 0;
    int32_t not_found_cnt = 0;
    int32_t repetition_count = DEFAULT_REPETITION_COUNT;
    int32_t option;

    printf("needle is %s, size %d\n", needle, sizeof(needle));

    /* Parse command line options */
    while ((option = getopt(argc, argv, "n:")) != -1) {
        if (option == 'n') {
            int32_t user_repetition_count = atoi(optarg);

            /* Check if the value is a string or zero */
            if (user_repetition_count == 0) {
                fprintf(stderr, "Error ... Invalid value for option '-n'.\n");
                exit(EXIT_FAILURE);
            }
            /* Check if the value is a negative number */
            if (user_repetition_count < MIN_REPETITION_COUNT) {
                fprintf(stderr, "Error ... Value for option '-n' cannot be a "
                                "number less than %d.\n", MIN_REPETITION_COUNT);
                exit(EXIT_FAILURE);
            }
            /* Check if the value is too large */
            if (user_repetition_count > MAX_REPETITION_COUNT) {
                fprintf(stderr, "Error ... Value for option '-n' cannot be "
                                "more than %d.\n", MAX_REPETITION_COUNT);
                exit(EXIT_FAILURE);
            }
            repetition_count = user_repetition_count;
        } else {
            exit(EXIT_FAILURE);
        }
    }

    srand(1);

    for (size_t i = 0; i < repetition_count; ++i) {
        /* Generate random hay, and, in turn, find a needle */
        gen_random_string(haystack, HAYSTACK_LEN);
        found_needle = strstr(haystack, needle);
        if (found_needle != NULL) {
            found_cnt++;
        } else {
            not_found_cnt++;
        }
    }

    /* Control printing */
    printf("CONTROL RESULT:\n");
    printf(" Found %d times. Not found %d times.\n", found_cnt, not_found_cnt);
}
