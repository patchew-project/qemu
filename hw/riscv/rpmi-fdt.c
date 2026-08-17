/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RISC-V RPMI device-tree helpers
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/riscv/rpmi-fdt.h"
#include "hw/misc/riscv_rpmi.h"
#include "system/device_tree.h"

void riscv_rpmi_fdt_add_mbox(void *fdt,
                             const RiscvRpmiFdtMboxConfig *cfg,
                             uint32_t *phandle,
                             uint32_t *mbox_handle)
{
    g_autofree char *name = NULL;
    hwaddr a2p_req_base, p2a_ack_base, p2a_req_base, a2p_ack_base;
    static const char * const regnames_all[RPMI_ALL_NUM_REGS] = {
        "a2p-req", "p2a-ack", "p2a-req", "a2p-ack", "a2p-doorbell"
    };
    static const char * const regnames_a2p[RPMI_A2P_NUM_REGS] = {
        "a2p-req", "p2a-ack", "a2p-doorbell"
    };

    a2p_req_base = cfg->shmem_base;
    p2a_ack_base = a2p_req_base + cfg->a2p_req_size;
    p2a_req_base = p2a_ack_base + cfg->a2p_req_size;
    a2p_ack_base = p2a_req_base + cfg->p2a_req_size;

    *mbox_handle = (*phandle)++;
    name = g_strdup_printf("/soc/mailbox@%" HWADDR_PRIx, cfg->shmem_base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,rpmi-shmem-mbox");
    qemu_fdt_setprop_cell(fdt, name, "riscv,slot-size", RPMI_QUEUE_SLOT_SIZE);
    qemu_fdt_setprop_cell(fdt, name, "#mbox-cells", 1);

    if (cfg->p2a_req_size) {
        qemu_fdt_setprop_string_array(fdt, name, "reg-names",
                                      (char **)&regnames_all,
                                      ARRAY_SIZE(regnames_all));
        qemu_fdt_setprop_cells(fdt, name, "reg",
            (uint32_t)(a2p_req_base >> 32), (uint32_t)a2p_req_base,
            0x0, cfg->a2p_req_size,
            (uint32_t)(p2a_ack_base >> 32), (uint32_t)p2a_ack_base,
            0x0, cfg->a2p_req_size,
            (uint32_t)(p2a_req_base >> 32), (uint32_t)p2a_req_base,
            0x0, cfg->p2a_req_size,
            (uint32_t)(a2p_ack_base >> 32), (uint32_t)a2p_ack_base,
            0x0, cfg->p2a_req_size,
            (uint32_t)(cfg->doorbell_base >> 32), (uint32_t)cfg->doorbell_base,
            0x0, cfg->doorbell_size);
    } else {
        qemu_fdt_setprop_string_array(fdt, name, "reg-names",
                                      (char **)&regnames_a2p,
                                      ARRAY_SIZE(regnames_a2p));
        qemu_fdt_setprop_cells(fdt, name, "reg",
            (uint32_t)(a2p_req_base >> 32), (uint32_t)a2p_req_base,
            0x0, cfg->a2p_req_size,
            (uint32_t)(p2a_ack_base >> 32), (uint32_t)p2a_ack_base,
            0x0, cfg->a2p_req_size,
            (uint32_t)(cfg->doorbell_base >> 32), (uint32_t)cfg->doorbell_base,
            0x0, cfg->doorbell_size);
    }

    qemu_fdt_setprop_cells(fdt, name, "phandle", *mbox_handle);
}

void riscv_rpmi_fdt_add_service(void *fdt, hwaddr shmem_base,
                                const char *node_name,
                                const char *compatible,
                                uint32_t mbox_handle,
                                uint32_t service_group,
                                bool has_mpxy_channel,
                                uint32_t mpxy_channel)
{
    g_autofree char *name = g_strdup_printf("/soc/mailbox@%" HWADDR_PRIx "/%s",
                                            shmem_base, node_name);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", compatible);
    qemu_fdt_setprop_cells(fdt, name, "mboxes", mbox_handle, service_group);
    if (has_mpxy_channel) {
        qemu_fdt_setprop_cell(fdt, name, "riscv,sbi-mpxy-channel-id",
                              mpxy_channel);
    }
}

void riscv_rpmi_fdt_add_service_node(void *fdt, hwaddr shmem_base,
                                     const RiscvRpmiServiceConfig *service,
                                     uint32_t mbox_handle)
{
    riscv_rpmi_fdt_add_service(fdt, shmem_base, service->node_name,
                               service->compatible, mbox_handle,
                               service->service_group,
                               service->has_mpxy_channel,
                               service->mpxy_channel);
}
