==================
QATzip Compression
==================
In scenarios with limited network bandwidth, the ``QATzip`` solution can help
users save a lot of host CPU resources by accelerating compression and
decompression through the Intel QuickAssist Technology(``QAT``) hardware.

``QATzip`` is a user space library which builds on top of the Intel QuickAssist
Technology user space library, to provide extended accelerated compression and
decompression services.

For more ``QATzip`` introduction, please refer to `QATzip Introduction
<https://github.com/intel/QATzip?tab=readme-ov-file#introductionl>`_

QATzip Compression Framework
============================

::

  +----------------+
  | MultiFd Thread |
  +-------+--------+
          |
          | compress/decompress
  +-------+--------+
  | QATzip library |
  +-------+--------+
          |
  +-------+--------+
  |  QAT library   |
  +-------+--------+
          |         user space
  --------+---------------------
          |         kernel space
   +------+-------+
   |  QAT  Driver |
   +------+-------+
          |
   +------+-------+
   | QAT Devices  |
   +--------------+


QATzip Installation
-------------------

The ``QATzip`` installation package has been integrated into some Linux
distributions and can be installed directly. For example, the Ubuntu Server
24.04 LTS system can be installed using below command

.. code-block:: shell

   #apt search qatzip
   libqatzip-dev/noble 1.2.0-0ubuntu3 amd64
     Intel QuickAssist user space library development files

   libqatzip3/noble 1.2.0-0ubuntu3 amd64
     Intel QuickAssist user space library

   qatzip/noble,now 1.2.0-0ubuntu3 amd64 [installed]
     Compression user-space tool for Intel QuickAssist Technology

   #sudo apt install libqatzip-dev libqatzip3 qatzip

If your system does not support the ``QATzip`` installation package, you can
use the source code to build and install, please refer to `QATzip source code installation
<https://github.com/intel/QATzip?tab=readme-ov-file#build-intel-quickassist-technology-driver>`_

QAT Hardware Deployment
-----------------------

``QAT`` supports physical functions(PFs) and virtual functions(VFs) for
deployment, and users can configure ``QAT`` resources for migration according
to actual needs. For more details about ``QAT`` deployment, please refer to
`Intel QuickAssist Technology Documentation
<https://intel.github.io/quickassist/index.html>`_

For more ``QAT`` hardware introduction, please refer to `intel-quick-assist-technology-overview
<https://www.intel.com/content/www/us/en/architecture-and-technology/intel-quick-assist-technology-overview.html>`_

How To Use QATzip Compression
=============================

1 - Install ``QATzip`` library

2 - Build ``QEMU`` with ``--enable-qatzip`` parameter

  E.g. configure --target-list=x86_64-softmmu --enable-kvm ``--enable-qatzip``

3 - Set ``migrate_set_parameter multifd-compression qatzip``

4 - Set ``migrate_set_parameter multifd-qatzip-level comp_level``, the default
comp_level value is 1, and it supports levels from 1 to 9


Performance Testing with QATzip
===============================

Testing environment is being set as below:

VM configuration:16 vCPU, 64G memory;

VM Workload: all vCPUs are idle and 54G memory is filled with Silesia data;

QAT Devices: 4;

Sender migration parameters:

.. code-block:: shell

    migrate_set_capability multifd on
    migrate_set_parameter multifd-channels 2/4/8
    migrate_set_parameter max-bandwidth 1G/10G
    migrate_set_parameter multifd-compression qatzip/zstd

Receiver migration parameters:

.. code-block:: shell

    migrate_set_capability multifd on
    migrate_set_parameter multifd-channels 2
    migrate_set_parameter multifd-compression qatzip/zstd

max-bandwidth: 1 GBps (Gbytes/sec)

.. code-block:: text

    |-----------|--------|---------|----------|------|------|
    |2 Channels |Total   |down     |throughput| send | recv |
    |           |time(ms)|time(ms) |(mbps)    | cpu %| cpu% |
    |-----------|--------|---------|----------|------|------|
    |qatzip     |   21607|       77|      8051|    88|   125|
    |-----------|--------|---------|----------|------|------|
    |zstd       |   78351|       96|      2199|   204|    80|
    |-----------|--------|---------|----------|------|------|

    |-----------|--------|---------|----------|------|------|
    |4 Channels |Total   |down     |throughput| send | recv |
    |           |time(ms)|time(ms) |(mbps)    | cpu %| cpu% |
    |-----------|--------|---------|----------|------|------|
    |qatzip     |   20336|       25|      8557|   110|   190|
    |-----------|--------|---------|----------|------|------|
    |zstd       |   39324|       31|      4389|   406|   160|
    |-----------|--------|---------|----------|------|------|

    |-----------|--------|---------|----------|------|------|
    |8 Channels |Total   |down     |throughput| send | recv |
    |           |time(ms)|time(ms) |(mbps)    | cpu %| cpu% |
    |-----------|--------|---------|----------|------|------|
    |qatzip     |   20208|       22|      8613|   125|   300|
    |-----------|--------|---------|----------|------|------|
    |zstd       |   20515|       22|      8438|   800|   340|
    |-----------|--------|---------|----------|------|------|

max-bandwidth: 10 GBps (Gbytes/sec)

.. code-block:: text

    |-----------|--------|---------|----------|------|------|
    |2 Channels |Total   |down     |throughput| send | recv |
    |           |time(ms)|time(ms) |(mbps)    | cpu %| cpu% |
    |-----------|--------|---------|----------|------|------|
    |qatzip     |   22450|       77|      7748|    80|   125|
    |-----------|--------|---------|----------|------|------|
    |zstd       |   78339|       76|      2199|   204|    80|
    |-----------|--------|---------|----------|------|------|

    |-----------|--------|---------|----------|------|------|
    |4 Channels |Total   |down     |throughput| send | recv |
    |           |time(ms)|time(ms) |(mbps)    | cpu %| cpu% |
    |-----------|--------|---------|----------|------|------|
    |qatzip     |   13017|       24|     13401|   180|   285|
    |-----------|--------|---------|----------|------|------|
    |zstd       |   39466|       21|      4373|   406|   160|
    |-----------|--------|---------|----------|------|------|

    |-----------|--------|---------|----------|------|------|
    |8 Channels |Total   |down     |throughput| send | recv |
    |           |time(ms)|time(ms) |(mbps)    | cpu %| cpu% |
    |-----------|--------|---------|----------|------|------|
    |qatzip     |   10255|       22|     17037|   280|   590|
    |-----------|--------|---------|----------|------|------|
    |zstd       |   20126|       77|      8595|   810|   340|
    |-----------|--------|---------|----------|------|------|

max-bandwidth: 1.25 GBps (Gbytes/sec)

.. code-block:: text

    |-----------|--------|---------|----------|----------|------|------|
    |8 Channels |Total   |down     |throughput|pages per | send | recv |
    |           |time(ms)|time(ms) |(mbps)    |second    | cpu %| cpu% |
    |-----------|--------|---------|----------|----------|------|------|
    |qatzip     |   16630|       28|     10467|   2940235|   160|   360|
    |-----------|--------|---------|----------|----------|------|------|
    |zstd       |   20165|       24|      8579|   2391465|   810|   340|
    |-----------|--------|---------|----------|----------|------|------|
    |none       |   46063|       40|     10848|    330240|    45|    85|
    |-----------|--------|---------|----------|----------|------|------|

If the user has enabled compression in live migration, using QAT can save the
host CPU resources.

When compression is enabled, the bottleneck of migration is usually the
compression throughput on the sender side, since CPU decompression throughput
is higher than compression, some reference data
https://github.com/inikep/lzbench, so more CPU resources need to be allocated
to the sender side.

Summary:

1. In the 1GBps case, QAT only uses 88% CPU utilization to reach 1GBps, but
   ZSTD needs 800%.

2. In the 10Gbps case, QAT uses 180% CPU utilization to reach 10GBps. but ZSTD
   still cannot reach 10Gbps even if it uses 810%.

3. The QAT decompression CPU utilization is higher than compression and ZSTD,
   because:

   a. When using QAT compression, the data needs to be copied to the QAT memory
   (for DMA operations), and the same for decompression. However,
   do_user_addr_fault will be triggered during decompression because the QAT
   decompressed data is copied to the VM address space for the first time, in
   addition, both compression and decompression are processed by QAT and do not
   consume CPU resources, so the CPU utilization of the receiver is slightly
   higher than the sender.

   b. Since zstd decompression decompresses data directly into the VM address
   space, there is one less memory copy than QAT, so the CPU utilization on the
   receiver is better than QAT. For the 1GBps case, the receiver CPU
   utilization is 125%, and the memory copy occupies ~80% of CPU utilization.

How To Choose Between QATzip and QPL
====================================
Starting from Intel 4th Gen Intel Xeon Scalable processors, codenamed Sapphire
Rapids processor(``SPR``), it supports multiple build-in accelerators including
``QAT`` and ``IAA``, the former can accelerate ``QATzip``, and the latter is
used to accelerate ``QPL``.

Here are some suggestions:

1 - If your live migration scenario is limited network bandwidth and ``QAT``
hardware resources exceed ``IAA``, then use the ``QATzip`` method, which
can save a lot of host CPU resources for compression.

2 - If your system cannot support shared virtual memory(SVM) technology, please
use ``QATzip`` method because ``QPL`` performance is not good without SVM
support.

3 - For other scenarios, please use the ``QPL`` method first.
