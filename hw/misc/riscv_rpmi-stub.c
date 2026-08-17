/*
 * RISC-V RPMI stubs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/misc/riscv_rpmi.h"

DeviceState *riscv_rpmi_create(const RiscvRpmiConfig *cfg G_GNUC_UNUSED,
                               Error **errp)
{
    error_setg(errp, "RISC-V RPMI support is not compiled in");
    return NULL;
}
