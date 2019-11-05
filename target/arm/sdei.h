/*
 * ARM SDEI emulation external interfaces
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2019. All rights reserved.
 *
 * Authors:
 *    Heyi Guo <guoheyi@huawei.com>
 *    Jingyi Wang <wangjingyi11@huawei.com>
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

#ifndef QEMU_SDEI_H
#define QEMU_SDEI_H

#include <linux/kvm.h>
#include <linux/arm_sdei.h>
#include "hw/core/cpu.h"

#define SDEI_MAX_REQ        SDEI_1_0_FN(0x12)

void sdei_handle_request(CPUState *cs, struct kvm_run *run);

#endif
