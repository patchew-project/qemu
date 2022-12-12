/*
 * Virtio Support
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-access.h"
#include "cpu.h"

#if defined(TARGET_PPC64) || defined(TARGET_ARM)
#define LEGACY_VIRTIO_IS_BIENDIAN 1
#endif

bool virtio_access_is_big_endian(VirtIODevice *vdev)
{
#if defined(LEGACY_VIRTIO_IS_BIENDIAN)
    return virtio_is_big_endian(vdev);
#elif TARGET_BIG_ENDIAN
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        /* Devices conforming to VIRTIO 1.0 or later are always LE. */
        return false;
    }
    return true;
#else
    return false;
#endif
}

uint32_t virtio_config_readb(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldub_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_readw(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = lduw_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_readl(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldl_p(vdev->config + addr);
    return val;
}

void virtio_config_writeb(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stb_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_writew(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stw_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_writel(VirtIODevice *vdev, uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stl_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

uint32_t virtio_config_modern_readb(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldub_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_modern_readw(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = lduw_le_p(vdev->config + addr);
    return val;
}

uint32_t virtio_config_modern_readl(VirtIODevice *vdev, uint32_t addr)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val;

    if (addr + sizeof(val) > vdev->config_len) {
        return (uint32_t)-1;
    }

    k->get_config(vdev, vdev->config);

    val = ldl_le_p(vdev->config + addr);
    return val;
}

void virtio_config_modern_writeb(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint8_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stb_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_modern_writew(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint16_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stw_le_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

void virtio_config_modern_writel(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data)
{
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    uint32_t val = data;

    if (addr + sizeof(val) > vdev->config_len) {
        return;
    }

    stl_le_p(vdev->config + addr, val);

    if (k->set_config) {
        k->set_config(vdev, vdev->config);
    }
}

