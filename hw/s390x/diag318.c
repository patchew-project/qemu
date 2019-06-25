/*
 * DIAGNOSE 0x318 functions for reset and migration
 *
 * Copyright IBM, Corp. 2019
 *
 * Authors:
 *  Collin Walling <walling@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version. See the COPYING file in the top-level directory.
 */

#include "hw/s390x/diag318.h"
#include "qapi/error.h"
#include "kvm_s390x.h"
#include "sysemu/kvm.h"

static int diag318_post_load(void *opaque, int version_id)
{
    DIAG318State *d = opaque;

    kvm_s390_set_diag318_info(d->info);
    return 0;
}

static int diag318_pre_save(void *opaque)
{
    DIAG318State *d = opaque;

    kvm_s390_get_diag318_info(&d->info);
    return 0;
}

static bool diag318_needed(void *opaque)
{
    return s390_has_feat(S390_FEAT_DIAG318);
}

const VMStateDescription vmstate_diag318 = {
    .name = "vmstate_diag318",
    .post_load = diag318_post_load,
    .pre_save = diag318_pre_save,
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = diag318_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(info, DIAG318State),
        VMSTATE_END_OF_LIST()
    }
};

static void s390_diag318_reset(DeviceState *dev)
{
    kvm_s390_set_diag318_info(0);
}

static void s390_diag318_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s390_diag318_reset;
    dc->vmsd = &vmstate_diag318;
    dc->hotpluggable = false;
    /* Reason: Set automatically during IPL */
    dc->user_creatable = false;
}

static const TypeInfo s390_diag318_info = {
    .class_init = s390_diag318_class_init,
    .parent = TYPE_DEVICE,
    .name = TYPE_S390_DIAG318,
    .instance_size = sizeof(DIAG318State),
};

static void s390_diag318_register_types(void)
{
    type_register_static(&s390_diag318_info);
}

type_init(s390_diag318_register_types)
