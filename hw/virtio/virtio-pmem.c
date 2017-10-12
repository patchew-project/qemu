/*
 * Virtio pmem device
 *
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-pmem.h"


static void virtio_pmem_system_reset(void *opaque)
{

}

static void virtio_pmem_flush(VirtIODevice *vdev, VirtQueue *vq)
{
    /* VirtIOPMEM  *vm = VIRTIO_PMEM(vdev); */
}


static void virtio_pmem_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOPMEM *pmem = VIRTIO_PMEM(vdev);
    struct virtio_pmem_config *pmemcfg = (struct virtio_pmem_config *) config;

    pmemcfg->start = pmem->start;
    pmemcfg->size  = pmem->size;
    pmemcfg->align = pmem->align;
}

static uint64_t virtio_pmem_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    virtio_add_feature(&features, VIRTIO_PMEM_PLUG);
    return features;
}



static void virtio_pmem_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice   *vdev   = VIRTIO_DEVICE(dev);
    VirtIOPMEM     *pmem   = VIRTIO_PMEM(dev);
    MachineState   *ms     = MACHINE(qdev_get_machine());
    MemoryRegion   *mr;
    PCMachineState *pcms   =
        PC_MACHINE(object_dynamic_cast(OBJECT(ms), TYPE_PC_MACHINE));

    if (!pmem->memdev) {
        error_setg(errp, "virtio-pmem not set");
        return;
    }

    pmem->start = pcms->hotplug_memory.base;

    /*if (!pcmc->broken_reserved_end) {
            pmem->size = memory_region_size(&pcms->hotplug_memory.mr);
    }*/


    mr               = host_memory_backend_get_memory(pmem->memdev, errp);
    pmem->size       = memory_region_size(mr);
    pmem->align      = memory_region_get_alignment(mr);

    virtio_init(vdev, TYPE_VIRTIO_PMEM, VIRTIO_ID_PMEM,
                sizeof(struct virtio_pmem_config));

    pmem->rq_vq = virtio_add_queue(vdev, 128, virtio_pmem_flush);

    host_memory_backend_set_mapped(pmem->memdev, true);
    qemu_register_reset(virtio_pmem_system_reset, pmem);
}

static void virtio_mem_check_memdev(Object *obj, const char *name, Object *val,
                                    Error **errp)
{

    if (host_memory_backend_is_mapped(MEMORY_BACKEND(val))) {

        char *path = object_get_canonical_path_component(val);
        error_setg(errp, "Can't use already busy memdev: %s", path);
        g_free(path);
        return;
    }

    qdev_prop_allow_set_link_before_realize(obj, name, val, errp);
}

static void virtio_pmem_instance_init(Object *obj)
{

    VirtIOPMEM *vm = VIRTIO_PMEM(obj);

    object_property_add_link(obj, "memdev", TYPE_MEMORY_BACKEND,
                             (Object **)&vm->memdev,
                             (void *) virtio_mem_check_memdev,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);

}


static void virtio_pmem_class_init(ObjectClass *klass, void *data)
{
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    vdc->realize      =  virtio_pmem_realize;
    vdc->get_config   =  virtio_pmem_get_config;
    vdc->get_features =  virtio_pmem_get_features;
}

static TypeInfo virtio_pmem_info = {
    .name          = TYPE_VIRTIO_PMEM,
    .parent        = TYPE_VIRTIO_DEVICE,
    .class_size    = sizeof(VirtIOPMEM),
    .class_init    = virtio_pmem_class_init,
    .instance_init = virtio_pmem_instance_init,
};


static void virtio_register_types(void)
{
    type_register_static(&virtio_pmem_info);
}

type_init(virtio_register_types)
