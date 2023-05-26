/*
 * QEMU KVM support -- RISC-V specific functions.
 *
 * Copyright (c) 2020 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_KVM_RISCV_H
#define QEMU_KVM_RISCV_H

void kvm_riscv_reset_vcpu(RISCVCPU *cpu);
void kvm_riscv_set_irq(RISCVCPU *cpu, int irq, int level);

#define KVM_DEV_RISCV_AIA_GRP_CONFIG            0
#define KVM_DEV_RISCV_AIA_CONFIG_MODE           0
#define KVM_DEV_RISCV_AIA_CONFIG_IDS            1
#define KVM_DEV_RISCV_AIA_CONFIG_SRCS           2
#define KVM_DEV_RISCV_AIA_CONFIG_GROUP_BITS     3
#define KVM_DEV_RISCV_AIA_CONFIG_GROUP_SHIFT    4
#define KVM_DEV_RISCV_AIA_CONFIG_HART_BITS      5
#define KVM_DEV_RISCV_AIA_CONFIG_GUEST_BITS     6
#define KVM_DEV_RISCV_AIA_MODE_EMUL             0
#define KVM_DEV_RISCV_AIA_MODE_HWACCEL          1
#define KVM_DEV_RISCV_AIA_MODE_AUTO             2
#define KVM_DEV_RISCV_AIA_IDS_MIN               63
#define KVM_DEV_RISCV_AIA_IDS_MAX               2048
#define KVM_DEV_RISCV_AIA_SRCS_MAX              1024
#define KVM_DEV_RISCV_AIA_GROUP_BITS_MAX        8
#define KVM_DEV_RISCV_AIA_GROUP_SHIFT_MIN       24
#define KVM_DEV_RISCV_AIA_GROUP_SHIFT_MAX       56
#define KVM_DEV_RISCV_AIA_HART_BITS_MAX         16
#define KVM_DEV_RISCV_AIA_GUEST_BITS_MAX        8

#define KVM_DEV_RISCV_AIA_GRP_ADDR              1
#define KVM_DEV_RISCV_AIA_ADDR_APLIC            0
#define KVM_DEV_RISCV_AIA_ADDR_IMSIC(__vcpu)    (1 + (__vcpu))
#define KVM_DEV_RISCV_AIA_ADDR_MAX              \
        (1 + KVM_DEV_RISCV_APLIC_MAX_HARTS)

#define KVM_DEV_RISCV_AIA_GRP_CTRL              2
#define KVM_DEV_RISCV_AIA_CTRL_INIT             0

#define KVM_DEV_RISCV_AIA_GRP_APLIC             3

#define KVM_DEV_RISCV_AIA_GRP_IMSIC             4

#endif
