/*
 * QEMU live migration - functions that need to be compiled target-specific
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/qapi-types-migration.h"
#include "migration.h"
#include CONFIG_DEVICES

#ifdef CONFIG_VFIO

#include "hw/vfio/vfio-common.h"

void populate_vfio_info(MigrationInfo *info)
{
    if (vfio_mig_active()) {
        info->has_vfio = true;
        info->vfio = g_malloc0(sizeof(*info->vfio));
        info->vfio->transferred = vfio_mig_bytes_transferred();
    }
}
#else

void populate_vfio_info(MigrationInfo *info) {}

#endif /* CONFIG_VFIO */
