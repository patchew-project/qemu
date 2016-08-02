/*
 * KVM-based ITS implementation for a GICv3-based system
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Pavel Fedin <p.fedin@samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"

#define TYPE_KVM_ARM_ITS "arm-its-kvm"
#define KVM_ARM_ITS(obj) OBJECT_CHECK(GICv3ITSState, (obj), TYPE_KVM_ARM_ITS)

static int kvm_its_send_msi(GICv3ITSState *s, uint32_t value, uint16_t devid)
{
    struct kvm_msi msi;

    msi.address_lo = s->gits_translater_gpa & 0xFFFFFFFFULL;
    msi.address_hi = s->gits_translater_gpa >> 32;
    msi.data = value;
    msi.flags = KVM_MSI_VALID_DEVID;
    msi.devid = devid;
    memset(msi.pad, 0, sizeof(msi.pad));

    return kvm_vm_ioctl(kvm_state, KVM_SIGNAL_MSI, &msi);
}

typedef struct ItsInitNotifierParams {
    Notifier notifier;
    GICv3ITSState *s;
} ItsInitNotifierParams;

/**
 *
 * We currently do not use kvm_arm_register_device to provide
 * the kernel with the vITS control frame base address since the
 * KVM_DEV_ARM_VGIC_CTRL_INIT init MUST be called after the
 * KVM_ARM_SET_DEVICE_ADDR and the kvm_arm_register_device
 * infra does not allow this.
 */
static void its_notify(Notifier *notifier, void *data)
{
    ItsInitNotifierParams *p = DO_UPCAST(ItsInitNotifierParams,
                                         notifier, notifier);
    GICv3ITSState *s = p->s;
    MemoryRegion *mr = &s->iomem_its_cntrl;
    MemoryRegionSection mrs;
    struct kvm_device_attr attr;
    uint64_t addr;
    int ret;

    mrs = memory_region_find(mr, 0, 1);
    addr = mrs.offset_within_address_space;

    attr.flags = 0;
    attr.group = KVM_DEV_ARM_VGIC_GRP_ADDR;
    attr.attr = KVM_VGIC_ITS_ADDR_TYPE;
    attr.addr =  (uintptr_t)&addr;

    s->gits_translater_gpa = addr + ITS_CONTROL_SIZE + 0x40;

    ret = kvm_device_ioctl(s->dev_fd, KVM_SET_DEVICE_ATTR, attr);
    if (ret) {
        error_setg_errno(&error_fatal, -ret,
                         "not able to set base address for vITS ctrl frame");
    }

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                      KVM_DEV_ARM_VGIC_CTRL_INIT, NULL, true);
}

static void kvm_arm_its_realize(DeviceState *dev, Error **errp)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);
    ItsInitNotifierParams *p = g_new(ItsInitNotifierParams, 1);

    s->dev_fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_ARM_VGIC_ITS, false);
    if (s->dev_fd < 0) {
        error_setg_errno(errp, -s->dev_fd, "error creating in-kernel ITS");
        return;
    }

    gicv3_its_init_mmio(s, NULL);

    p->notifier.notify = its_notify;
    p->s = s;
    qemu_add_machine_init_done_notifier(&p->notifier);

    kvm_arm_msi_use_devid = true;
    kvm_gsi_routing_allowed = kvm_has_gsi_routing();
    kvm_gsi_direct_mapping = false;
    kvm_msi_via_irqfd_allowed = kvm_irqfds_enabled();
}

static void kvm_arm_its_init(Object *obj)
{
    GICv3ITSState *s = KVM_ARM_ITS(obj);

    object_property_add_link(obj, "parent-gicv3",
                             "kvm-arm-gicv3", (Object **)&s->gicv3,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static void kvm_arm_its_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    GICv3ITSCommonClass *icc = ARM_GICV3_ITS_COMMON_CLASS(klass);

    dc->realize = kvm_arm_its_realize;
    icc->send_msi = kvm_its_send_msi;
}

static const TypeInfo kvm_arm_its_info = {
    .name = TYPE_KVM_ARM_ITS,
    .parent = TYPE_ARM_GICV3_ITS_COMMON,
    .instance_size = sizeof(GICv3ITSState),
    .instance_init = kvm_arm_its_init,
    .class_init = kvm_arm_its_class_init,
};

static void kvm_arm_its_register_types(void)
{
    type_register_static(&kvm_arm_its_info);
}

type_init(kvm_arm_its_register_types)
