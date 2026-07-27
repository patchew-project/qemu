.. SPDX-License-Identifier: GPL-2.0-or-later
.. _igb-migration:

igb VF Migration
----------------

The igb device supports an experimental VF migration interface that allows
the ``igb-vfio-pci`` variant driver to migrate VF state during live
migration. This is enabled with the ``x-vf-migration`` property::

  -device igb,x-vf-migration=on,...

When enabled, each emulated VF advertises a vendor-specific PCI capability
(cap id 0x09) with a magic signature (``0x4D494742`` / "MIGB") that the
variant driver probes at bind time. The capability contains an interface
version number, the BAR index hosting the migration register region, and
feature flags indicating which migration features are supported.

This feature is experimental and the ``x-`` prefix indicates the interface
may change.
