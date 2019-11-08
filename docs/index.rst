.. QEMU documentation master file, created by
   sphinx-quickstart on Thu Jan 31 16:40:14 2019.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Welcome to QEMU's documentation!
================================

.. Non-rst documentation

`QEMU User Documentation <qemu-doc.html>`_

`QEMU QMP Reference Manual <qemu-doc.html>`_

`QEMU Guest Agent Protocol Reference <qemu-doc.html>`_

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   interop/index
   specs/index

.. The QEMU Developer's Guide is not included in the main documentation because
   users don't need it.
.. toctree::
   :hidden:

   devel/index

.. Hidden documents that still need to be reviewed and moved to the appropriate
   section of the documentation.
.. toctree::
   :hidden:

   arm-cpu-features
   cpu-hotplug
   microvm
   pr-manager
   virtio-net-failover
   virtio-pmem
