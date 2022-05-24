============================
VFIO Device Migration Plugins
============================

Contents:
=========
* Introduction
* Usage
* Plugin based VFIO Live Migration Flow
* Interface Description between QEMU and Plugins

Introduction:
============

Plugin based VFIO live migration is an extension to VFIO live migration
mechanism, which is described in ``docs/devel/vfio-migration.rst``. It provides
an out-of-band migration solution for PCIe functions exposed by Infrastructure
Processing Units (IPU) and Data Processing Units (DPU).

IPU/DPU usually has an SoC in the backend where a Linux system usually runs
out-of-band agents to provision and configure the interfaces and communicate
with a host management stack such as gRPC or JSON-RPC. Plugin based VFIO live
migration leverage the agents in the Soc to save/restore PCIe device states.

This is a new feature for VFIO live migration and it allows device vendors to
develop out-of-tree plugins that can be dynamically loaded into a running QEMU
process during VFIO passthrough devices live migration.

This document describes the interfaces between QEMU VFIO live migration
framework and the plugins.

Usage:
======

An example to use VFIO migration plugin is as the following command line:

-device vfio-pci-emu,x-enable-migration=on,x-plugin-path=$plugin_path,x-plugin-arg=$plugin_arg

Where,

- the 'x-enable-migration' controls whether the VFIO device supports live
  migration (Not supported by default).

- 'x-plugin-path' indicates the path of the plugin on the host.

- 'x-plugin-arg' is a parameter required by QEMU to load and use the out-of-tree
  plugin, if the plugin communicates with the backend on IPU/DPU by network,
  this parameter should be <IP: Port>.

Plugin based VFIO Live Migration Flow:
======================================

The following ASCII graph describes the overall component relationship:

 +----------------------------------------------------+
 | QEMU                                               |
 | +------------------------------------------------+ |
 | |        VFIO Live Migration Framework           | |
 | |    +--------------------------------------+    | |
 | |    |         VFIOMigrationOps             |    | |
 | |    +-------^---------------------^--------+    | |
 | |            |                     |             | |
 | |    +-------v-------+     +-------v--------+    | |
 | |    | VFIO LM Based |     | VFIO LM Based  |    | |
 | |    |On Local Region|     |   On Plugin    |    | |
 | |    +-------^-------+     |     +----------+    | |
 | |            |             |     |Plugin Ops+----+-+------------+
 | |            |             +-----+----------+    | |            |
 | |            |                                   | |  +---------v----------+
 | +------------+-----------------------------------+ |  |  Vendor Specific   |
 |              |                                     |  |    Plugins(.so)    |
 +--------------+-------------------------------------+  +----------+---------+
  UserSpace     |                                                   |
----------------+---------------------------------------------      |
  Kernel        |                                                   |
                |                                                   |
     +----------v----------------------+                            |
     |        Kernel VFIO Driver       |                            |
     |    +-------------------------+  |                            |
     |    |                         |  |                            | Network
     |    | Vendor-Specific Driver  |  |                            |
     |    |                         |  |                            |
     |    +----------^--------------+  |                            |
     |               |                 |                            |
     +---------------+-----------------+                            |
                     |                                              |
                     |                                              |
---------------------+-----------------------------------------     |
  Hardware           |                                              |
                     |            +-----+-----+-----+----+-----+    |
          +----------v------+     | VF0 | VF1 | VF2 | ...| VFn |    |
          |   Traditional   |     +-----+-----+-----+----+-----+    |
          |  PCIe Devices   |     |                            |    |
          +-----------------+     |   +--------+------------+  |    |
                                  |   |        |   Agent    |<-+----+
                                  |   |        +------------+  |
                                  |   |                     |  |
                                  |   | SOC                 |  |
                                  |   +---------------------+  |
                                  | IPU/DPU                    |
                                  +----------------------------+

Two QEMU command line options (x-plugin-path and x-plugin-arg) are introduced to
specify the corresponding plugin and its parameters for a passthrough device.
If they are specified, the plugin will be loaded in vfio_migration_probe(),
which will check the plugin version and get the pointer to the plugin's
VFIOMigrationPluginOps. If any failure during the probing, the plugin will not
be loaded, and this PCIe device will be marked as no supporting of live
migration.

When live migration happens, VFIO live migration framework will invoke the
callbacks defined in VFIOMigrationPluginOps to save/restore the device states,
as described in the following section.

Interface Description between QEMU and Plugins:
=============================================

The interfaces between QEMU VFIO live migration framework and vendor-specific
plugin are defined as follows:

    - VFIOLMPluginGetVersion:
        This is a function type. Plugins must expose a function symbol named
        ``vfio_lm_get_plugin_version`` with this function type to return the
        interface version supported by the plugin.
    - VFIOLMPluginGetOps:
        This is a function type. Plugins must expose a function symbol named
        ``vfio_lm_get_plugin_ops`` with this function type to return a pointer
        to VFIOMigrationPluginOps struct.
    - VFIOMigrationPluginOps:
        This is a struct type containing a set of callbacks that plugin
        exposes. The callbacks will be invoked by QEMU VFIO during live
        migration for saving and restoring device states.

The interfaces are defined in include/hw/vfio/vfio-migration-plugin.h.

When QEMU loads a migration plugin, it will first find and invoke a function
symbol named ``vfio_lm_get_plugin_version`` to check the interface version that
plugin supports. The core code will refuse to load a plugin if it doesn't export
the symbol or the version doesn't match the one QEMU supports.

Then QEMU finds and invokes function symbol named ``vfio_lm_get_plugin_ops`` to
get vendor device-specific VFIOMigrationPluginOps which will be used for
saving/restoring device states.

VFIOMigrationPluginOps is defined as follows:

typedef struct VFIOMigrationPluginOps {
    void *(*init)(char *devid, char *arg);
    int (*save)(void *handle, uint8_t *state, uint64_t len);
    int (*load)(void *handle, uint8_t *state, uint64_t len);
    int (*update_pending)(void *handle, uint64_t *pending_bytes);
    int (*set_state)(void *handle, uint32_t value);
    int (*get_state)(void *handle, uint32_t *value);
    int (*cleanup)(void *handle);
} VFIOMigrationPluginOps;

Here:
    - init(): set the PCIe device BDF and args, and get the plugin handle.
    - save(): save the VFIO passthrough device states on the source.
    - load(): restore the VFIO passthrough device states on the destination.
    - set_state(): set the PCIe device states including SAVING, RUNNING,
                   STOP, and RESUMING.
    - get_state(): get the PCIe device states.
    - update_pending(): get the remaining bytes during data transfer.
    - cleanup(): unload the plugin and release some resources.
