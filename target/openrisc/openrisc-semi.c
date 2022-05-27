/*
 *  OpenRISC Semihosting syscall interface.
 *
 *  Copyright (c) 2022 Stafford Horne
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/gdbstub.h"
#include "sysemu/runstate.h"
#include "qemu/log.h"

#define HOSTED_EXIT 1
#define HOSTED_RESET 13

static void or1k_semi_return_u32(CPUOpenRISCState *env, uint32_t ret)
{
    cpu_set_gpr(env, 11, ret);
}

void do_or1k_semihosting(CPUOpenRISCState *env, uint32_t k)
{
    uint32_t result;

    switch (k) {
    case HOSTED_EXIT:
        gdb_exit(cpu_get_gpr(env, 3));
        exit(cpu_get_gpr(env, 3));
    case HOSTED_RESET:
#ifndef CONFIG_USER_ONLY
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
#endif
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "or1k-semihosting: unsupported "
                      "semihosting syscall %d\n", k);
        result = 0;
    }
    or1k_semi_return_u32(env, result);
}
