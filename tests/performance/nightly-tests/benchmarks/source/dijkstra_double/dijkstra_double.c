/*
 *  Source file of a benchmark program involving calculations of the
 *  shortest distances between a source node and all other nodes in a
 *  graph of n nodes in which all nxn distances are defined as "double".
 *  The number n can be given via command line, and the default is 2000.
 *  The algorithm used is Dijsktra's.
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

#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Number of columns and rows in all matrixes*/
#define DEFAULT_NODE_COUNT      2000
#define MIN_NODE_COUNT          3
#define MAX_NODE_COUNT          10000


int32_t closest_index(int32_t count, double *distances, bool *flags)
{
    int32_t closest;
    double minimum = DBL_MAX;

    for (size_t i = 0; i < count; i++) {
        if (flags[i] == false && distances[i] <= minimum) {
            closest = i;
            minimum = distances[i];
        }
    }

    return closest;
}

/**
 * Calculate the shortest distances from the source node using Dijkstra method.
 * @param (out) distances  An array of shortest distances from the source node.
 * @param (out) via  An array of nodes needed to be taken as the the last
 *                   before destination, for each destination.
 * @param (out) eccent  Eccentricity of the source node.
 * @param (in) count  The number of nodes.
 * @param (in) source  Source node.
 * @param (in) matrix  Distance matrix.
 */
void find_shortest_distances(double *distances, int32_t *via, double *eccent,
                             int32_t count, int32_t source, double **matrix)
{
    bool *flags;

    flags = (bool *)malloc(count * sizeof(bool));

    for (size_t i = 0; i < count; i++) {
        distances[i] = DBL_MAX;
        flags[i] = false;
    }

    distances[source] = 0.0;
    via[source] = source;

    for (size_t i = 0; i < count - 1; i++) {
        int32_t closest = closest_index(count, distances, flags);
        flags[closest] = true;
        for (size_t j = 0; j < count; j++) {
            if ((!flags[j]) &&
                    (matrix[closest][j]) &&
                    (distances[closest] != DBL_MAX) &&
                    (distances[j] > distances[closest] + matrix[closest][j])) {
                distances[j] = distances[closest] + matrix[closest][j];
                via[j] = closest;
            }
        }
    }

    *eccent = 0;
    for (size_t i = 0; i < count; i++) {
        if (*eccent < distances[i]) {
            *eccent = distances[i];
        }
    }

    free(flags);
}


void main(int argc, char *argv[])
{
    double **distance_matrix;
    double *shortest_distances;
    int32_t *via_node;
    int32_t node_count = DEFAULT_NODE_COUNT;
    int32_t source_node = 0;
    double node_eccentricity = 0.0;
    double range_factor = 999.0 / (double)(RAND_MAX);
    int32_t option;

    /* Parse command line options */
    while ((option = getopt(argc, argv, "n:")) != -1) {
        if (option == 'n') {
            int32_t user_node_count = atoi(optarg);

            /* Check if the value is a string or zero */
            if (user_node_count == 0) {
                fprintf(stderr, "Error ... Invalid value for option '-n'.\n");
                exit(EXIT_FAILURE);
            }
            /* Check if the value is a negative number */
            if (user_node_count < MIN_NODE_COUNT) {
                fprintf(stderr, "Error ... Value for option '-n' cannot be a "
                                "number less than %d.\n", MIN_NODE_COUNT);
                exit(EXIT_FAILURE);
            }
            /* Check if the value is too large */
            if (user_node_count > MAX_NODE_COUNT) {
                fprintf(stderr, "Error ... Value for option '-n' cannot be "
                                "more than %d.\n", MAX_NODE_COUNT);
                exit(EXIT_FAILURE);
            }
            node_count = user_node_count;
        } else {
            exit(EXIT_FAILURE);
        }
    }

    /* Allocate the memory space for all matrixes */
    distance_matrix = (double **)malloc(node_count * sizeof(double *));
    for (size_t i = 0; i < node_count; i++) {
        distance_matrix[i] = (double *)malloc(node_count * sizeof(double));
    }
    shortest_distances = (double *)malloc(node_count * sizeof(double));
    via_node = (int32_t *)malloc(node_count * sizeof(int32_t));

    /* Initialize helper arrays and populate distance_matrix */
    srand(1);
    for (size_t i = 0; i < node_count; i++) {
        shortest_distances[i] = 0.0;
        via_node[i] = -1;
        distance_matrix[i][i] = 0.0;
    }
    for (size_t i = 0; i < node_count; i++) {
        for (size_t j = i + 1; j < node_count; j++) {
            distance_matrix[i][j] = 1.0 + range_factor * (double)rand();
            distance_matrix[j][i] = distance_matrix[i][j];
        }
    }

    find_shortest_distances(shortest_distances, via_node, &node_eccentricity,
                            node_count, source_node, distance_matrix);

    /* Control printing */
    printf("CONTROL RESULT:\n");
    printf(" Distance matrix (top left part):\n");
    for (size_t i = 0; i < 3; i++) {
        for (size_t j = 0; j < 3; j++) {
            printf("    %7.2f", distance_matrix[i][j]);
        }
        printf("\n");
    }
    printf(" Source: %d (eccentricity: %f)\n",
           source_node, node_eccentricity);
    printf(" Destination   Distance   Via Node\n");
    for (size_t i = 0; i < 3; i++) {
        printf("  %5d       %7.2f       %4d\n",
               i, shortest_distances[i], via_node[i]);
    }

    /* Free all previously allocated space */
    for (size_t i = 0; i < node_count; i++) {
        free(distance_matrix[i]);
    }
    free(distance_matrix);
    free(shortest_distances);
    free(via_node);
}
