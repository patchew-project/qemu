/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RISC-V RPMI System Suspend service.
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
 */

#include "qemu/osdep.h"
#include "riscv_rpmi_internal.h"
#include "qemu/timer.h"
#include "system/runstate.h"

static const struct rpmi_system_suspend_type riscv_rpmi_syssusp_types[] = {
    {
        .type = RPMI_SYSSUSP_TYPE_SUSPEND_TO_RAM,
        .attr = RPMI_SYSSUSP_ATTRS_FLAGS_RESUMEADDR,
    },
};

static enum rpmi_error riscv_rpmi_syssusp_prepare(
    void *priv, rpmi_uint32_t hart_index,
    const struct rpmi_system_suspend_type *syssusp_type,
    rpmi_uint64_t resume_addr)
{
    RiscvRpmiState *s = priv;

    if (s->machine_ops && s->machine_ops->register_wakeup_support) {
        s->machine_ops->register_wakeup_support();
    }
    return RPMI_SUCCESS;
}

static rpmi_bool_t riscv_rpmi_syssusp_ready(void *priv,
                                            rpmi_uint32_t hart_index)
{
    return true;
}

static void riscv_rpmi_syssusp_finalize(
    void *priv, rpmi_uint32_t hart_index,
    const struct rpmi_system_suspend_type *syssusp_type,
    rpmi_uint64_t resume_addr)
{
    RiscvRpmiState *s = priv;

    if (s->machine_ops && s->machine_ops->system_suspend) {
        s->machine_ops->system_suspend();
    }
}

static rpmi_bool_t riscv_rpmi_syssusp_can_resume(void *priv,
                                                 rpmi_uint32_t hart_index)
{
    RiscvRpmiState *s = priv;

    if (s->machine_ops && s->machine_ops->system_can_resume) {
        return s->machine_ops->system_can_resume();
    }

    return true;
}

static enum rpmi_error riscv_rpmi_syssusp_resume(
    void *priv, rpmi_uint32_t hart_index,
    const struct rpmi_system_suspend_type *syssusp_type,
    rpmi_uint64_t resume_addr)
{
    RiscvRpmiState *s = priv;

    s->syssusp_resume_hart_index = hart_index;
    s->syssusp_resume_addr = resume_addr;
    s->syssusp_resume_pending = true;
    return RPMI_SUCCESS;
}

static const struct rpmi_syssusp_platform_ops riscv_rpmi_syssusp_ops = {
    .system_suspend_prepare = riscv_rpmi_syssusp_prepare,
    .system_suspend_ready = riscv_rpmi_syssusp_ready,
    .system_suspend_finalize = riscv_rpmi_syssusp_finalize,
    .system_suspend_can_resume = riscv_rpmi_syssusp_can_resume,
    .system_suspend_resume = riscv_rpmi_syssusp_resume,
};

static void riscv_rpmi_wakeup_timer(void *opaque)
{
    RiscvRpmiState *s = opaque;

    if (s->syssusp_resume_pending) {
        riscv_rpmi_hsm_resume(s, s->syssusp_resume_hart_index,
                              s->syssusp_resume_addr);
        s->syssusp_resume_pending = false;
        s->syssusp_resume_addr = 0;
    }
}

static void riscv_rpmi_wakeup_notify(Notifier *notifier, void *data)
{
    RiscvRpmiState *s = container_of(notifier, RiscvRpmiState,
                                     wakeup_notifier);

    if (s->context) {
        rpmi_context_process_group_events(s->context,
                                          RPMI_SRVGRP_SYSTEM_SUSPEND);
    }
    if (s->wakeup_timer) {
        /*
         * Wakeup notifiers run before vCPUs resume. Defer the HSM
         * kick so OpenSBI can leave WFI after QEMU restarts execution.
         */
        timer_mod(s->wakeup_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 10);
    }
}

static bool riscv_rpmi_syssusp_create(RiscvRpmiState *s,
                                      struct rpmi_service_group **group,
                                      Error **errp)
{
    if (!s->hsm) {
        error_setg(errp, "RPMI system suspend service requires HSM service");
        return false;
    }

    if (s->machine_ops && s->machine_ops->register_wakeup_support) {
        s->machine_ops->register_wakeup_support();
    }
    if (!s->wakeup_timer) {
        s->wakeup_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                       riscv_rpmi_wakeup_timer, s);
    }
    if (!s->wakeup_notifier_registered) {
        s->wakeup_notifier.notify = riscv_rpmi_wakeup_notify;
        qemu_register_wakeup_notifier(&s->wakeup_notifier);
        s->wakeup_notifier_registered = true;
    }

    *group = rpmi_service_group_syssusp_create(
        s->hsm, ARRAY_SIZE(riscv_rpmi_syssusp_types),
        riscv_rpmi_syssusp_types, &riscv_rpmi_syssusp_ops, s);
    if (!*group) {
        error_setg(errp, "failed to create RPMI system suspend service group");
        return false;
    }

    return true;
}

static void riscv_rpmi_syssusp_destroy(RiscvRpmiState *s)
{
    if (s->wakeup_notifier_registered) {
        notifier_remove(&s->wakeup_notifier);
        s->wakeup_notifier_registered = false;
    }
    if (s->wakeup_timer) {
        timer_free(s->wakeup_timer);
        s->wakeup_timer = NULL;
    }

    if (s->syssusp_group) {
        rpmi_service_group_syssusp_destroy(s->syssusp_group);
        s->syssusp_group = NULL;
    }
}

bool riscv_rpmi_syssusp_add(RiscvRpmiState *s, Error **errp)
{
    struct rpmi_service_group *group;

    if (s->syssusp_group) {
        error_setg(errp, "duplicate RPMI system suspend descriptor");
        return false;
    }

    if (!riscv_rpmi_syssusp_create(s, &group, errp)) {
        return false;
    }

    if (!riscv_rpmi_context_add_group(s, group, "system suspend", errp)) {
        s->syssusp_group = group;
        riscv_rpmi_syssusp_destroy(s);
        return false;
    }

    s->syssusp_group = group;
    return true;
}

void riscv_rpmi_syssusp_remove(RiscvRpmiState *s)
{
    riscv_rpmi_context_remove_group(s, s->syssusp_group);
    riscv_rpmi_syssusp_destroy(s);
}
