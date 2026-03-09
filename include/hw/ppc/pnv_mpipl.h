/*
 * Emulation of MPIPL (Memory Preserving Initial Program Load), aka fadump
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PNV_MPIPL_H
#define PNV_MPIPL_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"

typedef struct MpiplPreservedState MpiplPreservedState;

/* Preserved state to be saved in PnvMachineState */
struct MpiplPreservedState {
    /* skiboot_base will be valid only after OPAL sends relocated base to SBE */
    hwaddr     skiboot_base;
    bool       is_next_boot_mpipl;
};

#endif
