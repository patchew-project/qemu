/*
 * Emulation of MPIPL (Memory Preserving Initial Program Load), aka fadump
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/runstate.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_mpipl.h"

void do_mpipl_preserve(PnvMachineState *pnv)
{
    /* Mark next boot as Memory-preserving boot */
    pnv->mpipl_state.is_next_boot_mpipl = true;

    /*
     * Do a guest reset.
     * Next reset will see 'is_next_boot_mpipl' as true, and trigger MPIPL
     *
     * Requirement:
     * GUEST_RESET is expected to NOT clear the memory, as is the case when
     * this is merged
     */
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}
