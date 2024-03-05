/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TARGET_ANY_CPU_H
#define TARGET_ANY_CPU_H

#include "cpu-qom.h"
#include "exec/cpu-defs.h"

#define DUMMY_CPU_TYPE_SUFFIX "-" TYPE_DUMMY_CPU
#define DUMMY_CPU_TYPE_NAME(name) (name DUMMY_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_DUMMY_CPU

struct CPUArchState {
    /* nothing here */
};

struct ArchCPU {
    CPUState parent_obj;

    CPUArchState env;
};

#include "exec/cpu-all.h" /* FIXME remove once exec/ headers cleaned */

#endif
