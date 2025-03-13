/*
 * Firmware Assisted Dump in PSeries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/ppc/spapr.h"

/*
 * Handle the "FADUMP_CMD_REGISTER" command in 'ibm,configure-kernel-dump'
 *
 * Returns:
 *  * RTAS_OUT_HW_ERROR: Not implemented/Misc issue such as memory access
 *                       failures
 */
uint32_t do_fadump_register(void)
{
    /* WIP: FADUMP_CMD_REGISTER implemented in future patch */

    return RTAS_OUT_HW_ERROR;
}
