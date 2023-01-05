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

#define S390_TOPOLOGY_POLARITY_HORIZONTAL      0x00
#define S390_TOPOLOGY_POLARITY_VERTICAL_LOW    0x01
#define S390_TOPOLOGY_POLARITY_VERTICAL_MEDIUM 0x02
#define S390_TOPOLOGY_POLARITY_VERTICAL_HIGH   0x03

#define S390_TOPOLOGY_SHARED    0x00
#define S390_TOPOLOGY_DEDICATED 0x01

#endif
