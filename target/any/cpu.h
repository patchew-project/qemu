/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TARGET_ANY_CPU_H
#define TARGET_ANY_CPU_H

#include "exec/cpu-defs.h"

#define ANY_CPU_TYPE_SUFFIX "-" TYPE_ANY_CPU
#define ANY_CPU_TYPE_NAME(name) (name ANY_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_ANY_CPU

struct CPUArchState {
    /* nothing here */
};

struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUNegativeOffsetState neg;
    CPUArchState env;
};

#include "exec/cpu-all.h"

#endif
