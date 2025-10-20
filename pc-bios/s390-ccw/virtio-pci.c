/*
 * Functionality for virtio-pci
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clp.h"
#include "pci.h"
#include "helper.h"
#include "s390-ccw.h"
#include "virtio.h"
#include "bswap.h"
#include "virtio-pci.h"
#include "s390-time.h"
#include <stdio.h>

/* Variable offsets used for reads/writes to modern memory region BAR 4 */
uint32_t common_offset;
uint32_t device_offset;
uint32_t notify_offset;
uint32_t notify_mult;
uint16_t q_notify_offset;

static int virtio_pci_set_status(VDev *vdev, uint8_t status)
{
    uint64_t status64 = status;

    return pci_write(vdev->pci_fh, VPCI_C_OFFSET_STATUS, status64, 1);
}

static int virtio_pci_get_status(VDev *vdev, uint8_t *status)
{
    uint64_t status64;
    int rc;

    rc = pci_read(vdev->pci_fh, VPCI_C_OFFSET_STATUS, 4, &status64, 1);
    if (rc) {
        puts("Failed to read virtio-pci status");
        return rc;
    }

    *status = (uint8_t) status64;
    return 0;
}

static int virtio_pci_get_hfeatures(VDev *vdev, uint64_t *features)
{
    uint64_t feat0, feat1;
    uint32_t selector;
    int rc;

    selector = bswap32(0);
    rc = pci_write(vdev->pci_fh, VPCI_C_OFFSET_DFSELECT, selector, 4);
    rc |= pci_read(vdev->pci_fh, VPCI_C_OFFSET_DF, 4, &feat0, 4);
    feat0 = bswap32(feat0);

    selector = bswap32(1);
    rc |= pci_write(vdev->pci_fh, VPCI_C_OFFSET_DFSELECT, selector, 4);
    rc |= pci_read(vdev->pci_fh, VPCI_C_OFFSET_DF, 4, &feat1, 4);
    feat1 = bswap32(feat1);

    *features = feat1 << 32;
    *features |= feat0;

    return rc;
}

static int virtio_pci_set_gfeatures(VDev *vdev)
{
    uint64_t feats;
    uint32_t selector;
    int rc;

    selector = bswap32(0);
    rc = pci_write(vdev->pci_fh, VPCI_C_OFFSET_GFSELECT, selector, 4);

    feats = bswap32((uint64_t)vdev->guest_features[1]);
    rc |= pci_write(vdev->pci_fh, VPCI_C_OFFSET_GF, feats, 4);

    selector = bswap32(1);
    rc |= pci_write(vdev->pci_fh, VPCI_C_OFFSET_GFSELECT, selector, 4);

    feats = bswap32((uint64_t)vdev->guest_features[0]);
    rc |= pci_write(vdev->pci_fh, VPCI_C_OFFSET_GF, feats, 4);

    return rc;
}

static int virtio_pci_get_blk_config(VDev *vdev)
{
    return pci_read(vdev->pci_fh, device_offset, 4, (uint64_t *)&vdev->config.blk,
                    sizeof(VirtioBlkConfig));

}

int virtio_pci_set_selected_vq(VDev *vdev, uint16_t queue_num)
{
    uint16_t le_queue_num;

    le_queue_num = bswap16(queue_num);
    return pci_write(vdev->pci_fh, VPCI_C_OFFSET_Q_SELECT, (uint64_t)le_queue_num, 2);
}

int virtio_pci_set_queue_size(VDev *vdev, uint16_t queue_size)
{
    uint16_t le_queue_size;

    le_queue_size = bswap16(queue_size);
    return pci_write(vdev->pci_fh, VPCI_C_OFFSET_Q_SIZE, (uint64_t)le_queue_size, 2);
}

static int virtio_pci_set_queue_enable(VDev *vdev, uint16_t enabled)
{
    uint16_t le_enabled;

    le_enabled = bswap16(enabled);
    return pci_write(vdev->pci_fh, VPCI_C_OFFSET_Q_ENABLE, (uint64_t)le_enabled, 2);
}

static int set_pci_vq_addr(VDev *vdev, void* addr, uint64_t config_offset_lo)
{
    uint32_t le_lo, le_hi;
    uint32_t tmp;
    int rc;

    tmp = (uint32_t)(((uint64_t)addr) >> 32);
    le_hi = bswap32(tmp);

    tmp = (uint32_t)((uint64_t)addr & 0xFFFFFFFF);
    le_lo = bswap32(tmp);

    rc =  pci_write(vdev->pci_fh, config_offset_lo, (uint64_t)le_lo, 4);
    rc |=  pci_write(vdev->pci_fh, config_offset_lo + 4, (uint64_t)le_hi, 4);

    return rc;
}

/* virtio spec v1.1 para 4.1.2.1 */
void virtio_pci_id2type(VDev *vdev, uint16_t device_id)
{
    switch(device_id) {
    case 0x1001:
        vdev->type = VIRTIO_ID_BLOCK;
        break;
    case 0x1000: /* Other valid but currently unsupported virtio device types */
    case 0x1004:
    default:
        vdev->type = 0;
    }
}

/*
 * Read PCI configuration space to find the offset of the Common, Device, and
 * Notification memory regions within the modern memory space.
 * Returns 0 if success, 1 if a capability could not be located, or a
 * negative RC if the configuration read failed.
 */
static int virtio_pci_read_pci_cap_config(VDev *vdev)
{
    uint8_t pos;
    uint64_t data;

    /* Common cabilities */
    pos = find_cap_pos(vdev->pci_fh, VIRTIO_PCI_CAP_COMMON_CFG);
    if (!pos) {
        puts("Failed to locate PCI common configuration");
        return 1;
    }
    if (pci_read(vdev->pci_fh, pos + VIRTIO_PCI_CAP_OFFSET, 15, &data, 4)) {
        return -EIO;
    }
    common_offset = bswap32(data);

    /* Device cabilities */
    pos = find_cap_pos(vdev->pci_fh, VIRTIO_PCI_CAP_DEVICE_CFG);
    if (!pos) {
        puts("Failed to locate PCI device configuration");
        return 1;
    }
    if (pci_read(vdev->pci_fh, pos + VIRTIO_PCI_CAP_OFFSET, 15, &data, 4)) {
        return -EIO;
    }
    device_offset = bswap32(data);

    /* Notification cabilities */
    pos = find_cap_pos(vdev->pci_fh, VIRTIO_PCI_CAP_NOTIFY_CFG);
    if (!pos) {
        puts("Failed to locate PCI notification configuration");
        return 1;
    }
    if (pci_read(vdev->pci_fh, pos + VIRTIO_PCI_CAP_OFFSET, 15, &data, 4)) {
        return -EIO;
    }
    notify_offset = bswap32(data);

    if (pci_read(vdev->pci_fh, pos + VIRTIO_PCI_NOTIFY_CAP_MULT, 15, &data, 4)) {
        return -EIO;
    }
    notify_mult = bswap32(data);

    if (pci_read(vdev->pci_fh, device_offset + VPCI_C_OFFSET_Q_NOFF, 4, &data, 2)) {
        return -EIO;
    }
    q_notify_offset = bswap16(data);

    return 0;
}

int virtio_pci_reset(VDev *vdev)
{
    int rc;
    uint8_t status = VPCI_S_RESET;

    rc = virtio_pci_set_status(vdev, status);
    rc |= virtio_pci_get_status(vdev, &status);

    if (rc || status) {
        puts("Failed to reset virtio-pci device");
        return 1;
    }

    return 0;
}

int virtio_pci_setup(VDev *vdev)
{
    VRing *vr;
    int rc;
    uint64_t pci_features, data;
    uint8_t status;
    int i = 0;

    vdev->config.blk.blk_size = 0;
    vdev->guessed_disk_nature = VIRTIO_GDN_NONE;
    vdev->cmd_vr_idx = 0;

    if (virtio_reset(vdev)) {
        return -EIO;
    }

    status = VPCI_S_ACKNOWLEDGE;
    rc = virtio_pci_set_status(vdev, status);
    if (rc) {
        puts("Virtio-pci device Failed to ACKNOWLEDGE");
        return -EIO;
    }

    virtio_pci_read_pci_cap_config(vdev);
    if (rc) {
        printf("Invalid PCI capabilities");
        return -EIO;
    }

    switch (vdev->type) {
    case VIRTIO_ID_BLOCK:
        vdev->nr_vqs = 1;
        vdev->cmd_vr_idx = 0;
        virtio_pci_get_blk_config(vdev);
        break;
    default:
        puts("Unsupported virtio device");
        return -ENODEV;
    }

    status |= VPCI_S_DRIVER;
    rc = virtio_pci_set_status(vdev, status);
    if (rc) {
        puts("Set status failed");
        return -EIO;
    }

    /* Feature negotiation */
    rc = virtio_pci_get_hfeatures(vdev, &pci_features);
    if (rc) {
        puts("Failed to get feature bits");
        return -EIO;
    }

    rc = virtio_pci_set_gfeatures(vdev);
    if (rc) {
        puts("Failed to set feature bits");
        return -EIO;
    }

    /* Configure virt-queues for pci */
    for (i = 0; i < vdev->nr_vqs; i++) {
        VqInfo info = {
            .queue = (unsigned long long) virtio_get_ring_area() + (i * VIRTIO_RING_SIZE),
            .align = KVM_S390_VIRTIO_RING_ALIGN,
            .index = i,
            .num = 0,
        };

        vr = &vdev->vrings[i];
        rc = pci_read(vdev->pci_fh, VPCI_C_COMMON_NUMQ, 4, &data, 2);
        if (rc) {
            return rc;
        }

        info.num = data;
        vring_init(vr, &info);

        rc = virtio_pci_set_selected_vq(vdev, vr->id);
        if (rc) {
            puts("Failed to set selected virt-queue");
            return -EIO;
        }

        rc = virtio_pci_set_queue_size(vdev, 16);
        if (rc) {
            puts("Failed to set virt-queue size");
            return -EIO;
        }

        rc = set_pci_vq_addr(vdev, vr->desc, VPCI_C_OFFSET_Q_DESCLO);
        rc |= set_pci_vq_addr(vdev, vr->avail, VPCI_C_OFFSET_Q_AVAILLO);
        rc |= set_pci_vq_addr(vdev, vr->used, VPCI_C_OFFSET_Q_USEDLO);
        if (rc) {
            puts("Failed to set virt-queue address");
            return -EIO;
        }

        rc = virtio_pci_set_queue_enable(vdev, true);
        if (rc) {
            puts("Failed to set virt-queue enabled");
            return -EIO;
        }
    }

    status |= VPCI_S_FEATURES_OK | VPCI_S_DRIVER_OK;
    return virtio_pci_set_status(vdev, status);
}

int virtio_pci_setup_device(void)
{
    int rc;
    VDev *vdev = virtio_get_device();

    rc = enable_pci_function(&vdev->pci_fh);
    if (rc) {
        puts("Failed to enable PCI function");
        return rc;
    }

    return 0;
}

long virtio_pci_notify(uint32_t fhandle, int vq_id)
{
    uint64_t notice = 1;
    uint32_t offset = notify_offset + vq_id * q_notify_offset;

    return pci_write(fhandle, offset, notice, 4);
}
