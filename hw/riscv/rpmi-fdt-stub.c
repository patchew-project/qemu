/*
 * RISC-V RPMI device-tree helper stubs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/riscv/rpmi-fdt.h"

void riscv_rpmi_fdt_add_mbox(void *fdt G_GNUC_UNUSED,
                             const RiscvRpmiFdtMboxConfig *cfg G_GNUC_UNUSED,
                             uint32_t *phandle G_GNUC_UNUSED,
                             uint32_t *mbox_handle G_GNUC_UNUSED)
{
    g_assert_not_reached();
}
