/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_X86_WHPX_H
#define QEMU_X86_WHPX_H

int whpx_request_interrupt(uint32_t interrupt_type, uint32_t vector,
                           uint32_t vp_index, bool logical_dest_mode,
                           bool level_triggered);

int whpx_set_lint(CPUState* cpu);
#endif /* QEMU_X86_WHPX_H */
