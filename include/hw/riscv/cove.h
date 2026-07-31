/*
 * RISC-V Confidential VM Extension (CoVE)
 *
 * Copyright (c) 2026 Alibaba Group
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_COVE_H
#define HW_RISCV_COVE_H

/*
 * A CoVE guest is a TEE VM (TVM) whose memory and vCPU state are owned by
 * the TEE Security Manager (TSM) instead of the host. Subsystems outside of
 * hw/riscv/ have to behave differently for such a guest, so the state is
 * kept in target independent code.
 *
 * riscv_cove_vm_set_active() is called by the machine that implements CoVE
 * while its properties are parsed, before any device is created.
 */
bool riscv_cove_vm_active(void);
void riscv_cove_vm_set_active(bool active);

#endif /* HW_RISCV_COVE_H */
