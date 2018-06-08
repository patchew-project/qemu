#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "ui/console.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/display/ramfb.h"
#include "qapi/error.h"

/*
 * virtio-ramfb: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_RAMFB "virtio-ramfb"
#define VIRTIO_RAMFB(obj) \
        OBJECT_CHECK(VirtIORAMFB, (obj), TYPE_VIRTIO_RAMFB)

typedef struct VirtIORAMFB {
    VirtIOPCIProxy parent_obj;
    VirtIOGPU      vdev;
    RAMFBState     *ramfb;
} VirtIORAMFB;

static void virtio_ramfb_invalidate_display(void *opaque)
{
    VirtIORAMFB *vramfb = opaque;

    if (vramfb->vdev.enable) {
        virtio_gpu_ops.invalidate(&vramfb->vdev);
    }
}

static void virtio_ramfb_update_display(void *opaque)
{
    VirtIORAMFB *vramfb = opaque;
    VirtIOGPU *g = &vramfb->vdev;

    if (vramfb->vdev.enable) {
        virtio_gpu_ops.gfx_update(&vramfb->vdev);
    } else {
        ramfb_display_update(g->scanout[0].con, vramfb->ramfb);
    }
}

static int virtio_ramfb_ui_info(void *opaque, uint32_t idx, QemuUIInfo *info)
{
    VirtIORAMFB *vramfb = opaque;

    if (virtio_gpu_ops.ui_info) {
        return virtio_gpu_ops.ui_info(&vramfb->vdev, idx, info);
    }
    return -1;
}

static void virtio_ramfb_gl_block(void *opaque, bool block)
{
    VirtIORAMFB *vramfb = opaque;

    if (virtio_gpu_ops.gl_block) {
        virtio_gpu_ops.gl_block(&vramfb->vdev, block);
    }
}

static const GraphicHwOps virtio_ramfb_ops = {
    .invalidate = virtio_ramfb_invalidate_display,
    .gfx_update = virtio_ramfb_update_display,
    .ui_info = virtio_ramfb_ui_info,
    .gl_block = virtio_ramfb_gl_block,
};

static const VMStateDescription vmstate_virtio_ramfb = {
    .name = "virtio-ramfb",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        /* no pci stuff here, saving the virtio device will handle that */
        /* FIXME */
        VMSTATE_END_OF_LIST()
    }
};

/* RAMFB device wrapper around PCI device around virtio GPU */
static void virtio_ramfb_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIORAMFB *vramfb = VIRTIO_RAMFB(vpci_dev);
    VirtIOGPU *g = &vramfb->vdev;
    Error *err = NULL;
    int i;

    /* init virtio bits */
    qdev_set_parent_bus(DEVICE(g), BUS(&vpci_dev->bus));
    virtio_pci_force_virtio_1(vpci_dev);
    object_property_set_bool(OBJECT(g), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* init ramfb */
    vramfb->ramfb = ramfb_setup(errp);
    graphic_console_set_hwops(g->scanout[0].con, &virtio_ramfb_ops, vramfb);

    for (i = 0; i < g->conf.max_outputs; i++) {
        object_property_set_link(OBJECT(g->scanout[i].con),
                                 OBJECT(vpci_dev),
                                 "device", errp);
    }
}

static Property virtio_ramfb_properties[] = {
    DEFINE_VIRTIO_GPU_PCI_PROPERTIES(VirtIOPCIProxy),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_ramfb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->props = virtio_ramfb_properties;
    dc->vmsd = &vmstate_virtio_ramfb;
    dc->hotpluggable = false;

    k->realize = virtio_ramfb_realize;
    pcidev_k->class_id = PCI_CLASS_DISPLAY_OTHER;
}

static void virtio_ramfb_inst_initfn(Object *obj)
{
    VirtIORAMFB *dev = VIRTIO_RAMFB(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU);
}

static TypeInfo virtio_ramfb_info = {
    .name          = TYPE_VIRTIO_RAMFB,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(struct VirtIORAMFB),
    .instance_init = virtio_ramfb_inst_initfn,
    .class_init    = virtio_ramfb_class_init,
};

static void virtio_ramfb_register_types(void)
{
    type_register_static(&virtio_ramfb_info);
}

type_init(virtio_ramfb_register_types)
