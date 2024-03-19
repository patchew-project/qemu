===============
QPL Compression
===============
The Intel Query Processing Library (Intel ``QPL``) is an open-source library to
provide compression and decompression features and it is based on deflate
compression algorithm (RFC 1951).

The ``QPL`` compression relies on Intel In-Memory Analytics Accelerator(``IAA``)
and Shared Virtual Memory(``SVM``) technology, they are new features supported
from Intel 4th Gen Intel Xeon Scalable processors, codenamed Sapphire Rapids
processor(``SPR``).

For more ``QPL`` introduction, please refer to:

https://intel.github.io/qpl/documentation/introduction_docs/introduction.html

QPL Compression Framework
=========================

::

  +----------------+       +------------------+
  | MultiFD Service|       |accel-config tool |
  +-------+--------+       +--------+---------+
          |                         |
          |                         |
  +-------+--------+                | Setup IAA
  |  QPL library   |                | Resources
  +-------+---+----+                |
          |   |                     |
          |   +-------------+-------+
          |   Open IAA      |
          |   Devices +-----+-----+
          |           |idxd driver|
          |           +-----+-----+
          |                 |
          |                 |
          |           +-----+-----+
          +-----------+IAA Devices|
      Submit jobs     +-----------+
      via enqcmd


Intel In-Memory Analytics Accelerator (Intel IAA) Introduction
================================================================

Intel ``IAA`` is an accelerator that has been designed to help benefit
in-memory databases and analytic workloads. There are three main areas
that Intel ``IAA`` can assist with analytics primitives (scan, filter, etc.),
sparse data compression and memory tiering.

``IAA`` Manual Documentation:

https://www.intel.com/content/www/us/en/content-details/721858/intel-in-memory-analytics-accelerator-architecture-specification

IAA Device Enabling
-------------------

- Enabling ``IAA`` devices for platform configuration, please refer to:

https://www.intel.com/content/www/us/en/content-details/780887/intel-in-memory-analytics-accelerator-intel-iaa.html

- ``IAA`` device driver is ``Intel Data Accelerator Driver (idxd)``, it is
  recommended that the minimum version of Linux kernel is 5.18.

- Add ``"intel_iommu=on,sm_on"`` parameter to kernel command line
  for ``SVM`` feature enabling.

Here is an easy way to verify ``IAA`` device driver and ``SVM``, refer to:

https://github.com/intel/idxd-config/tree/stable/test

IAA Device Management
---------------------

The number of ``IAA`` devices will vary depending on the Xeon product model.
On a ``SPR`` server, there can be a maximum of 8 ``IAA`` devices, with up to
4 devices per socket.

By default, all ``IAA`` devices are disabled and need to be configured and
enabled by users manually.

Check the number of devices through the following command

.. code-block:: shell

  # lspci -d 8086:0cfe
  # 6a:02.0 System peripheral: Intel Corporation Device 0cfe
  # 6f:02.0 System peripheral: Intel Corporation Device 0cfe
  # 74:02.0 System peripheral: Intel Corporation Device 0cfe
  # 79:02.0 System peripheral: Intel Corporation Device 0cfe
  # e7:02.0 System peripheral: Intel Corporation Device 0cfe
  # ec:02.0 System peripheral: Intel Corporation Device 0cfe
  # f1:02.0 System peripheral: Intel Corporation Device 0cfe
  # f6:02.0 System peripheral: Intel Corporation Device 0cfe

IAA Device Configuration
------------------------

The ``accel-config`` tool is used to enable ``IAA`` devices and configure
``IAA`` hardware resources(work queues and engines). One ``IAA`` device
has 8 work queues and 8 processing engines, multiple engines can be assigned
to a work queue via ``group`` attribute.

One example of configuring and enabling an ``IAA`` device.

.. code-block:: shell

  # accel-config config-engine iax1/engine1.0 -g 0
  # accel-config config-engine iax1/engine1.1 -g 0
  # accel-config config-engine iax1/engine1.2 -g 0
  # accel-config config-engine iax1/engine1.3 -g 0
  # accel-config config-engine iax1/engine1.4 -g 0
  # accel-config config-engine iax1/engine1.5 -g 0
  # accel-config config-engine iax1/engine1.6 -g 0
  # accel-config config-engine iax1/engine1.7 -g 0
  # accel-config config-wq iax1/wq1.0 -g 0 -s 128 -p 10 -b 1 -t 128 -m shared -y user -n app1 -d user
  # accel-config enable-device iax1
  # accel-config enable-wq iax1/wq1.0

.. note::
   IAX is an early name for IAA

- The ``IAA`` device index is 1, use ``ls -lh /sys/bus/dsa/devices/iax*``
  command to query the ``IAA`` device index.

- 8 engines and 1 work queue are configured in group 0, so all compression jobs
  submitted to this work queue can be processed by all engines at the same time.

- Set work queue attributes including the work mode, work queue size and so on.

- Enable the ``IAA1`` device and work queue 1.0

.. note::
  Set work queue mode to shared mode, since ``QPL`` library only supports
  shared mode

For more detailed configuration, please refer to:

https://github.com/intel/idxd-config/tree/stable/Documentation/accfg

IAA Resources Allocation For Migration
--------------------------------------

There is no ``IAA`` resource configuration parameters for migration and
``accel-config`` tool configuration cannot directly specify the ``IAA``
resources used for migration.

``QPL`` will use all work queues that are enabled and set to shared mode,
and use all engines assigned to the work queues with shared mode.

By default, ``QPL`` will only use the local ``IAA`` device for compression
job processing. The local ``IAA`` device means that the CPU of the job
submission and the ``IAA`` device are on the same socket, so one CPU
can submit the jobs to up to 4 ``IAA`` devices.

Shared Virtual Memory(SVM) Introduction
=======================================

An ability for an accelerator I/O device to operate in the same virtual
memory space of applications on host processors. It also implies the
ability to operate from pageable memory, avoiding functional requirements
to pin memory for DMA operations.

When using ``SVM`` technology, users do not need to reserve memory for the
``IAA`` device and perform pin memory operation. The ``IAA`` device can
directly access data using the virtual address of the process.

For more ``SVM`` technology, please refer to:

https://docs.kernel.org/next/x86/sva.html


How To Use QPL Compression In Migration
=======================================

1 - Installation of ``accel-config`` tool and ``QPL`` library

  - Install ``accel-config`` tool from https://github.com/intel/idxd-config
  - Install ``QPL`` library from https://github.com/intel/qpl

2 - Configure and enable ``IAA`` devices and work queues via ``accel-config``

3 - Build ``Qemu`` with ``--enable-qpl`` parameter

  E.g. configure --target-list=x86_64-softmmu --enable-kvm ``--enable-qpl``

4 - Start VMs with ``sudo`` command or ``root`` permission

  Use the ``sudo`` command or ``root`` privilege to start the source and
  destination virtual machines, since migration service needs permission
  to access ``IAA`` hardware resources.

5 - Enable ``QPL`` compression during migration

  Set ``migrate_set_parameter multifd-compression qpl`` when migrating, the
  ``QPL`` compression does not support configuring the compression level, it
  only supports one compression level.

The Difference Between QPL And ZLIB
===================================

Although both ``QPL`` and ``ZLIB`` are based on the deflate compression
algorithm, and ``QPL`` can support the header and tail of ``ZLIB``, ``QPL``
is still not fully compatible with the ``ZLIB`` compression in the migration.

``QPL`` only supports 4K history buffer, and ``ZLIB`` is 32K by default. The
``ZLIB`` compressed data that ``QPL`` may not decompress correctly and
vice versa.

``QPL`` does not support the ``Z_SYNC_FLUSH`` operation in ``ZLIB`` streaming
compression, current ``ZLIB`` implementation uses ``Z_SYNC_FLUSH``, so each
``multifd`` thread has a ``ZLIB`` streaming context, and all page compression
and decompression are based on this stream. ``QPL`` cannot decompress such data
and vice versa.

The introduction for ``Z_SYNC_FLUSH``, please refer to:

https://www.zlib.net/manual.html

The Best Practices
==================

When the virtual machine's pages are not populated and the ``IAA`` device is
used, I/O page faults occur, which can impact performance due to a large number
of flush ``IOTLB`` operations.

Since the normal pages on the source side are all populated, ``IOTLB`` caused
by I/O page fault will not occur. On the destination side, a large number
of normal pages need to be loaded, so it is recommended to add ``-mem-prealloc``
parameter on the destination side.
