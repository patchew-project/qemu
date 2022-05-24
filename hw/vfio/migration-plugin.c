/*
 * QEMU VFIO Migration Support
 *
 * Copyright Intel Corporation, 2022
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"

#include "hw/vfio/vfio-common.h"
#include "migration/qemu-file.h"
#include "qapi/error.h"
#include "hw/vfio/vfio-migration-plugin.h"
#include "sysemu/sysemu.h"
#include "trace.h"

#define CHUNK_SIZE (1024 * 1024)

static int vfio_migration_load_plugin(VFIODevice *vbasedev)
{
    char *path = vbasedev->desc.path;
    VFIOMigration *migration = vbasedev->migration;
    VFIOMigrationPlugin *plugin = NULL;
    VFIOLMPluginGetVersion vfio_lm_get_plugin_version = NULL;
    VFIOLMPluginGetOps vfio_lm_get_plugin_ops = NULL;

    plugin = g_malloc0(sizeof(VFIOMigrationPlugin));
    if (!plugin) {
        error_report("%s: Error allocating buffer", __func__);
        return -ENOMEM;
    }

    plugin->module = g_module_open(path, G_MODULE_BIND_LOCAL);
    if (!plugin->module) {
        error_report("Failed to load VFIO migration plugin:%s", path);
        g_free(plugin);
        return -1;
    }

    if (!g_module_symbol(plugin->module, "vfio_lm_get_plugin_version",
                        (void *)&vfio_lm_get_plugin_version)) {
        error_report("Failed to load plugin ops %s: %s", path,
                    g_module_error());
        goto err;
    }

    if (vfio_lm_get_plugin_version() != VFIO_LM_PLUGIN_API_VERSION) {
        error_report("Invalid VFIO Plugin API Version %s : %s", path,
                     g_module_error());
        goto err;
    }

    if (!g_module_symbol(plugin->module, "vfio_lm_get_plugin_ops",
                        (void *)&vfio_lm_get_plugin_ops)) {
        error_report("Failed to load plugin ops %s: %s", path,
                     g_module_error());
        goto err;
    }

    plugin->ops = vfio_lm_get_plugin_ops();
    if (!plugin->ops) {
        error_report("Failed to Get Plugin Ops: %s", path);
        goto err;
    }

    migration->plugin = plugin;

    return 0;

err:
    g_module_close(plugin->module);
    g_free(plugin);
    plugin = NULL;
    return -1;
}

static int vfio_migration_save_load_setup_plugin(VFIODevice *vbasedev)
{
    char *arg = vbasedev->desc.arg;
    VFIOMigrationPlugin *plugin = vbasedev->migration->plugin;

    /* The name is BDF for PCIe device */
    plugin->handle = plugin->ops->init(vbasedev->name, arg);
    if (!plugin->handle) {
        error_report("Failed to init: %s", vbasedev->desc.path);
        return -1;
    }

    return 0;
}

static void vfio_migration_cleanup_plugin(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIOMigrationPlugin *plugin = migration->plugin;

    if (plugin->ops->cleanup) {
        plugin->ops->cleanup(plugin->handle);
        plugin->handle = NULL;
    }

    if (migration->plugin->module) {
        g_module_close(migration->plugin->module);
        migration->plugin->module = NULL;
    }

    g_free(migration->plugin);
    migration->plugin = NULL;
}

static int vfio_migration_update_pending_plugin(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;
    VFIOMigrationPlugin *plugin = migration->plugin;
    uint64_t pending_bytes = 0;
    int ret = -1;

    ret = plugin->ops->update_pending(plugin->handle, &pending_bytes);
    if (ret) {
        migration->pending_bytes = 0;
        error_report("%s: Failed to get pending size", __func__);
        return ret;
    }
    migration->pending_bytes = pending_bytes;
    trace_vfio_update_pending(vbasedev->name, pending_bytes);
    return 0;
}

static int vfio_migration_set_state_plugin(VFIODevice *vbasedev, uint32_t mask,
                                           uint32_t value)
{
    int ret = -1;
    uint32_t device_state = 0;
    VFIOMigrationPlugin *plugin = vbasedev->migration->plugin;

    ret = plugin->ops->get_state(plugin->handle, &device_state);
    if (ret) {
        error_report("%s: Get device state error", vbasedev->name);
        return ret;
    }

    device_state = (device_state & mask) | value;

    if (!VFIO_DEVICE_STATE_VALID(device_state)) {
        return -EINVAL;
    }

    ret = plugin->ops->set_state(plugin->handle, device_state);
    if (ret) {
        error_report("%s: Device in error state 0x%x", vbasedev->name,
                     value);
        return ret;
    }

    vbasedev->migration->device_state = device_state;
    trace_vfio_migration_set_state(vbasedev->name, device_state);
    return 0;
}

static int vfio_migration_save_buffer_plugin(QEMUFile *f, VFIODevice *vbasedev,
                                   uint64_t *size)
{
    int ret = 0;
    VFIOMigrationPlugin *plugin = vbasedev->migration->plugin;
    uint64_t data_size, tmp_size;

    ret = plugin->ops->update_pending(plugin->handle, &data_size);
    if (ret < 0) {
        error_report("%s: Failed to get pending size", __func__);
        return ret;
    }

    qemu_put_be64(f, data_size);
    tmp_size = data_size;

    trace_vfio_save_buffer_plugin(vbasedev->name, data_size);
    while (tmp_size) {
        uint64_t sz = tmp_size <= CHUNK_SIZE ? tmp_size : CHUNK_SIZE;
        void *buf = g_try_malloc(sz);

        if (!buf) {
            error_report("%s: Error allocating buffer", __func__);
            return -ENOMEM;
        }

        ret = plugin->ops->save(plugin->handle, buf, sz);
        if (ret) {
            error_report("%s:Failed saving device state", __func__);
            g_free(buf);
            return ret;
        }

        qemu_put_buffer(f, buf, sz);
        g_free(buf);
        tmp_size -= sz;
    }

    ret = qemu_file_get_error(f);
    if (!ret && size) {
        *size = data_size;
    }

    return ret;
}

static int vfio_migration_load_buffer_plugin(QEMUFile *f, VFIODevice *vbasedev,
                            uint64_t data_size)
{
    int ret = 0;
    VFIOMigrationPlugin *plugin = vbasedev->migration->plugin;

    trace_vfio_load_state_device_data_plugin(vbasedev->name, data_size);
    while (data_size) {
        uint64_t sz = data_size <= CHUNK_SIZE ? data_size : CHUNK_SIZE;
        void *buf = g_try_malloc(sz);

        if (!buf) {
            error_report("%s: Error allocating buffer", __func__);
            return -ENOMEM;
        }

        qemu_get_buffer(f, buf, sz);
        ret = plugin->ops->load(plugin->handle, buf, sz);
        g_free(buf);
        if (ret < 0) {
            error_report("%s: Error loading device state", vbasedev->name);
            return ret;
        }

        data_size -= sz;
    }

    return ret;
}

static VFIOMigrationOps vfio_plugin_method = {
    .save_setup = vfio_migration_save_load_setup_plugin,
    .load_setup = vfio_migration_save_load_setup_plugin,
    .update_pending = vfio_migration_update_pending_plugin,
    .save_buffer = vfio_migration_save_buffer_plugin,
    .load_buffer = vfio_migration_load_buffer_plugin,
    .set_state = vfio_migration_set_state_plugin,
    .cleanup = vfio_migration_cleanup_plugin
};

int vfio_migration_probe_plugin(VFIODevice *vbasedev)
{
    VFIOMigration *migration = vbasedev->migration;

    if (vfio_migration_load_plugin(vbasedev)) {
        error_report("vfio migration plugin probe failed");
        return -1;
    }

    migration->ops = &vfio_plugin_method;
    trace_vfio_migration_probe_plugin(vbasedev->name, vbasedev->desc.path,
                                      vbasedev->desc.arg);
    return 0;
}
