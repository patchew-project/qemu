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

extern bool sdei_enabled;

void sdei_handle_request(CPUState *cs, struct kvm_run *run);

/*
 * Trigger an SDEI event bound to an interrupt.
 * Return true if event has been triggered successfully.
 * Return false if event has not been triggered for some reason.
 */
bool trigger_sdei_by_irq(int cpu, int irq);

/*
 * Notify callback prototype; the argument "bind" tells whether it is a bind
 * operation or unbind one.
 */
typedef void QemuSDEIBindNotify(void *opaque, int irq,
                                int32_t event, bool bind);
/*
 * Register a notify callback for a specific interrupt bind operation; the
 * client will be both notified by bind and unbind operation.
 */
void qemu_register_sdei_bind_notifier(QemuSDEIBindNotify *func,
                                      void *opaque, int irq);
/*
 * Unregister a notify callback for a specific interrupt bind operation.
 */
void qemu_unregister_sdei_bind_notifier(QemuSDEIBindNotify *func,
                                        void *opaque, int irq);
#endif
