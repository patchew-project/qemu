/*
 * Emulation of MPIPL (Memory Preserving Initial Program Load), aka fadump
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PNV_MPIPL_H
#define PNV_MPIPL_H

#include "qemu/osdep.h"

typedef struct MpiplPreservedState MpiplPreservedState;

/* Preserved state to be saved in PnvMachineState */
struct MpiplPreservedState {
    bool       is_next_boot_mpipl;
};

#endif
