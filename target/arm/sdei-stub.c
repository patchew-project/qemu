/*
 * QEMU ARM SDEI specific function stubs
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2019. All rights reserved.
 *
 * Author: Heyi Guo <guoheyi@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sdei.h"

bool sdei_enabled;

void sdei_handle_request(CPUState *cs, struct kvm_run *run)
{
    run->hypercall.args[0] = SDEI_NOT_SUPPORTED;
    return;
}

/*
 * Trigger an SDEI event bound to an interrupt.
 * Return true if event has been triggered successfully.
 * Return false if event has not been triggered for some reason.
 */
bool trigger_sdei_by_irq(int cpu, int irq)
{
    return false;
}

/*
 * Register a notify callback for a specific interrupt bind operation; the
 * client will be both notified by bind and unbind operation.
 */
void qemu_register_sdei_bind_notifier(QemuSDEIBindNotify *func,
                                      void *opaque, int irq)
{
}

/*
 * Unregister a notify callback for a specific interrupt bind operation.
 */
void qemu_unregister_sdei_bind_notifier(QemuSDEIBindNotify *func,
                                        void *opaque, int irq)
{
}
