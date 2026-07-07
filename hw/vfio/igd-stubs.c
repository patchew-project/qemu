/*
 * IGD device quirks stubs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qerror.h"
#include "pci.h"
#include "pci-quirks.h"

void vfio_probe_igd_bar0_quirk(VFIOPCIDevice *vdev, int nr)
{
    return;
}

bool vfio_probe_igd_config_quirk(VFIOPCIDevice *vdev, Error **errp)
{
    return true;
}

void vfio_igd_legacy_rom_quirk(PCIDevice *pdev, uint8_t *ptr, uint32_t size)
{
    return;
}
