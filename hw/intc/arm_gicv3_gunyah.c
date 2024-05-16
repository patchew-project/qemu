/*
 * QEMU Gunyah hypervisor support
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/intc/arm_gicv3_common.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "sysemu/gunyah.h"
#include "sysemu/gunyah_int.h"
#include "sysemu/runstate.h"
#include "gicv3_internal.h"
#include "vgic_common.h"
#include "migration/blocker.h"
#include "qom/object.h"
#include "target/arm/cpregs.h"
#include "qemu/event_notifier.h"

struct GUNYAHARMGICv3Class {
    ARMGICv3CommonClass parent_class;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#define TYPE_GUNYAH_ARM_GICV3 "gunyah-arm-gicv3"
typedef struct GUNYAHARMGICv3Class GUNYAHARMGICv3Class;

/* This is reusing the GICv3State typedef from ARM_GICV3_ITS_COMMON */
DECLARE_OBJ_CHECKERS(GICv3State, GUNYAHARMGICv3Class,
                     GUNYAH_ARM_GICV3, TYPE_GUNYAH_ARM_GICV3)

static EventNotifier *irq_notify;

static void gunyah_arm_gicv3_set_irq(void *opaque, int irq, int level)
{
    GICv3State *s = (GICv3State *)opaque;

    if (irq < s->num_irq - GIC_INTERNAL) {
        event_notifier_set(&irq_notify[irq]);
    }
}

static void gunyah_arm_gicv3_realize(DeviceState *dev, Error **errp)
{
    GICv3State *s = GUNYAH_ARM_GICV3(dev);
    GUNYAHARMGICv3Class *ggc = GUNYAH_ARM_GICV3_GET_CLASS(s);
    Error *local_err = NULL;
    int i;
    GUNYAHState *state = get_gunyah_state();

    ggc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (s->revision != 3) {
        error_setg(errp, "unsupported GIC revision %d for in-kernel GIC",
                   s->revision);
        return;
    }

    gicv3_init_irqs_and_mmio(s, gunyah_arm_gicv3_set_irq, NULL);

    irq_notify = g_malloc_n(s->num_irq - GIC_INTERNAL, sizeof(EventNotifier));

    for (i = 0; i < s->num_irq - GIC_INTERNAL; ++i) {
        event_notifier_init(&irq_notify[i], 0);
        gunyah_add_irqfd(irq_notify[i].wfd, i, errp);
    }

    state->nr_irqs = s->num_irq - GIC_INTERNAL;
}

static void gunyah_arm_gicv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    GUNYAHARMGICv3Class *ggc = GUNYAH_ARM_GICV3_CLASS(klass);

    device_class_set_parent_realize(dc, gunyah_arm_gicv3_realize,
                                    &ggc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, NULL, NULL,
                                       &ggc->parent_phases);
}

static const TypeInfo gunyah_arm_gicv3_info = {
    .name = TYPE_GUNYAH_ARM_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(GICv3State),
    .class_init = gunyah_arm_gicv3_class_init,
    .class_size = sizeof(GUNYAHARMGICv3Class),
};

static void gunyah_arm_gicv3_register_types(void)
{
    type_register_static(&gunyah_arm_gicv3_info);
}

type_init(gunyah_arm_gicv3_register_types)
