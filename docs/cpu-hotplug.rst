===================
Virtual CPU hotplug
===================

A complete example of vCPU hotplug (and hot-unplug) using QMP
``device_add`` and ``device_del``.

vCPU hotplug
------------

(1) Launch QEMU as follows (note that the "maxcpus" is mandatory to
    allow vCPU hotplug)::

      $ qemu-system-x86_64 -display none -no-user-config -m 2048 \
          -nodefaults -monitor stdio -machine pc,accel=kvm,usb=off \
          -smp 1,maxcpus=2 -cpu IvyBridge-IBRS \
          -blockdev node-name=node-Base,driver=qcow2,file.driver=file,file.filename=./base.qcow2 \
          -device virtio-blk,drive=node-Base,id=virtio0 -qmp unix:/tmp/qmp-sock,server,nowait

(2) Run 'qmp-shell' (located in the source tree) to connect to the
    just-launched QEMU::

      $> ./qmp/qmp-shell -p -v /tmp/qmp-sock
      [...]
      (QEMU)

(3) Check which socket is free to allow hotplugging a CPU::

      (QEMU) query-hotpluggable-cpus
      {
          "execute": "query-hotpluggable-cpus",
          "arguments": {}
      }
      {
          "return": [
              {
                  "type": "IvyBridge-IBRS-x86_64-cpu",
                  "vcpus-count": 1,
                  "props": {
                      "socket-id": 1,
                      "core-id": 0,
                      "thread-id": 0
                  }
              },
              {
                  "qom-path": "/machine/unattached/device[0]",
                  "type": "IvyBridge-IBRS-x86_64-cpu",
                  "vcpus-count": 1,
                  "props": {
                      "socket-id": 0,
                      "core-id": 0,
                      "thread-id": 0
                  }
              }
          ]
      }
      (QEMU)

(4) We can see that socket 1 is free, so use `device_add` to hotplug
    "IvyBridge-IBRS-x86_64-cpu"::

      (QEMU) device_add id=cpu-2 driver=IvyBridge-IBRS-x86_64-cpu socket-id=1 core-id=0 thread-id=0
      {
          "execute": "device_add",
          "arguments": {
              "socket-id": 1,
              "driver": "IvyBridge-IBRS-x86_64-cpu",
              "id": "cpu-2",
              "core-id": 0,
              "thread-id": 0
          }
      }
      {
          "return": {}
      }
      (QEMU)

(5) Optionally, run QMP `query-cpus-fast` for some details about the
    vCPUs::

      (QEMU) query-cpus-fast
      {
          "execute": "query-cpus-fast",
          "arguments": {}
      }
      {
          "return": [
              {
                  "qom-path": "/machine/unattached/device[0]",
                  "target": "x86_64",
                  "thread-id": 11534,
                  "cpu-index": 0,
                  "props": {
                      "socket-id": 0,
                      "core-id": 0,
                      "thread-id": 0
                  },
                  "arch": "x86"
              },
              {
                  "qom-path": "/machine/peripheral/cpu-2",
                  "target": "x86_64",
                  "thread-id": 12106,
                  "cpu-index": 1,
                  "props": {
                      "socket-id": 1,
                      "core-id": 0,
                      "thread-id": 0
                  },
                  "arch": "x86"
              }
          ]
      }
      (QEMU)


vCPU hot-unplug
---------------

From the 'qmp-shell', invoke the QMP ``device_del`` command::

      (QEMU) device_del id=cpu-2
      {
          "execute": "device_del",
          "arguments": {
              "id": "cpu-2"
          }
      }
      {
          "return": {}
      }
      (QEMU)

.. note::
    vCPU hot-unplug requires guest cooperation; so the ``device_del``
    command above does not guarantee vCPU removal -- it's a "request to
    unplug".  At this point, the guest will get a System Control
    Interupt (SCI) and calls the ACPI handler for the affected vCPU
    device.  Then the guest kernel will bring the vCPU offline and tells
    QEMU to unplug it.
