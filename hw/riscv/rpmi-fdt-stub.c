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

void riscv_rpmi_fdt_add_service(void *fdt G_GNUC_UNUSED,
                                hwaddr shmem_base G_GNUC_UNUSED,
                                const char *node_name G_GNUC_UNUSED,
                                const char *compatible G_GNUC_UNUSED,
                                uint32_t mbox_handle G_GNUC_UNUSED,
                                enum rpmi_servicegroup_id service_group
                                G_GNUC_UNUSED,
                                bool has_mpxy_channel G_GNUC_UNUSED,
                                uint32_t mpxy_channel G_GNUC_UNUSED)
{
    g_assert_not_reached();
}

void riscv_rpmi_fdt_add_service_node(void *fdt G_GNUC_UNUSED,
                                     hwaddr shmem_base G_GNUC_UNUSED,
                                     const RiscvRpmiServiceConfig *service
                                     G_GNUC_UNUSED,
                                     uint32_t mbox_handle G_GNUC_UNUSED)
{
    g_assert_not_reached();
}
