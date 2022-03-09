..
   Copyright (c) 2022, Linaro Limited
   Written by Alex Bennée

Writing VirtIO backends for QEMU
================================

This document attempts to outline the information a developer needs to
know to write backends for QEMU. It is specifically focused on
implementing VirtIO devices.

Front End Transports
--------------------

VirtIO supports a number of different front end transports. The
details of the device remain the same but there are differences in
command line for specifying the device (e.g. -device virtio-foo
and -device virtio-foo-pci). For example:

.. code:: c

  static const TypeInfo vhost_user_blk_info = {
      .name = TYPE_VHOST_USER_BLK,
      .parent = TYPE_VIRTIO_DEVICE,
      .instance_size = sizeof(VHostUserBlk),
      .instance_init = vhost_user_blk_instance_init,
      .class_init = vhost_user_blk_class_init,
  };

defines ``TYPE_VHOST_USER_BLK`` as a child of the generic
``TYPE_VIRTIO_DEVICE``. And then for the PCI device it wraps around the
base device (although explicitly initialising via
virtio_instance_init_common):

.. code:: c

  struct VHostUserBlkPCI {
      VirtIOPCIProxy parent_obj;
      VHostUserBlk vdev;
  };
   
  static void vhost_user_blk_pci_instance_init(Object *obj)
  {
      VHostUserBlkPCI *dev = VHOST_USER_BLK_PCI(obj);

      virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                  TYPE_VHOST_USER_BLK);
      object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                                "bootindex");
  }

  static const VirtioPCIDeviceTypeInfo vhost_user_blk_pci_info = {
      .base_name               = TYPE_VHOST_USER_BLK_PCI,
      .generic_name            = "vhost-user-blk-pci",
      .transitional_name       = "vhost-user-blk-pci-transitional",
      .non_transitional_name   = "vhost-user-blk-pci-non-transitional",
      .instance_size  = sizeof(VHostUserBlkPCI),
      .instance_init  = vhost_user_blk_pci_instance_init,
      .class_init     = vhost_user_blk_pci_class_init,
  };

Back End Implementations
------------------------

There are a number of places where the implementation of the backend
can be done:

* in QEMU itself
* in the host kernel (a.k.a vhost)
* in a separate process (a.k.a. vhost-user)

where a vhost-user implementation is being done the code in QEMU is
mainly boilerplate to handle the command line definition and
connection to the separate process with a socket (using the ``chardev``
functionality).

Implementing a vhost-user wrapper
---------------------------------

There are some classes defined that can wrap a lot of the common
vhost-user code in a ``VhostUserBackend``. For example:

.. code:: c

  struct VhostUserGPU {
      VirtIOGPUBase parent_obj;

      VhostUserBackend *vhost;
      ...
  };

  static void
  vhost_user_gpu_instance_init(Object *obj)
  {
      VhostUserGPU *g = VHOST_USER_GPU(obj);

      g->vhost = VHOST_USER_BACKEND(object_new(TYPE_VHOST_USER_BACKEND));
      object_property_add_alias(obj, "chardev",
                                OBJECT(g->vhost), "chardev");
  }

  static void
  vhost_user_gpu_device_realize(DeviceState *qdev, Error **errp)
  {
      VhostUserGPU *g = VHOST_USER_GPU(qdev);
      VirtIODevice *vdev = VIRTIO_DEVICE(g);

      vhost_dev_set_config_notifier(&g->vhost->dev, &config_ops);
      if (vhost_user_backend_dev_init(g->vhost, vdev, 2, errp) < 0) {
          return;
      }
      ...
  }

  static void
  vhost_user_gpu_class_init(ObjectClass *klass, void *data)
  {
      DeviceClass *dc = DEVICE_CLASS(klass);
      VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

      vdc->realize = vhost_user_gpu_device_realize;
      ...
  }

  static const TypeInfo vhost_user_gpu_info = {
      .name = TYPE_VHOST_USER_GPU,
      .parent = TYPE_VIRTIO_GPU_BASE,
      .instance_size = sizeof(VhostUserGPU),
      .instance_init = vhost_user_gpu_instance_init,
      .class_init = vhost_user_gpu_class_init,
      ...
  };

Here the ``TYPE_VHOST_USER_GPU`` is based off a shared base class
(``TYPE_VIRTIO_GPU_BASE`` which itself is based on
``TYPE_VIRTIO_DEVICE``). The chardev property is aliased to the
VhostUserBackend chardev so it can be specified on the command line
for this device.
 
