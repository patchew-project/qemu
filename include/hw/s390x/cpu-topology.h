/*
 * CPU Topology
 *
 * Copyright IBM Corp. 2022
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#ifndef HW_S390X_CPU_TOPOLOGY_H
#define HW_S390X_CPU_TOPOLOGY_H

#define S390_TOPOLOGY_CPU_IFL   0x03

enum s390_topology_polarity {
    POLARITY_HORIZONTAL,
    POLARITY_VERTICAL,
    POLARITY_VERTICAL_LOW = 1,
    POLARITY_VERTICAL_MEDIUM,
    POLARITY_VERTICAL_HIGH,
    POLARITY_MAX,
};

#endif
