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

/* Variable offsets used for reads/writes to modern memory regions */
VirtioPciCap c_cap; /* Common capabilities  */
VirtioPciCap d_cap; /* Device capabilities  */
VirtioPciCap n_cap; /* Notify capabilities  */
uint32_t notify_mult;
uint16_t q_notify_offset;

static int virtio_pci_set_status(VDev *vdev, uint8_t status)
{
    int rc = pci_write_byte(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_STATUS,
                             c_cap.bar, status);
    if (rc) {
        puts("Failed to write virtio-pci status");
        return -EIO;
    }

    return 0;
}

static int virtio_pci_get_status(VDev *vdev, uint8_t *status)
{
    int rc = pci_read_byte(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_STATUS,
                           c_cap.bar, status);
    if (rc) {
        puts("Failed to read virtio-pci status");
        return -EIO;
    }

    return 0;
}

/*
 * Find the position of the capability config within PCI configuration
 * space for a given cfg type.  Return the position if found, otherwise 0.
 */
static uint8_t find_cap_pos(uint32_t fhandle, uint8_t cfg_type)
{
    uint8_t next, cfg;
    int rc;

    rc = pci_read_byte(fhandle, PCI_CAPABILITY_LIST, PCI_CFGBAR, &next);
    rc |= pci_read_byte(fhandle, next + 3, PCI_CFGBAR, &cfg);

    while (!rc && (cfg != cfg_type) && next) {
        rc = pci_read_byte(fhandle, next + 1, PCI_CFGBAR, &next);
        rc |= pci_read_byte(fhandle, next + 3, PCI_CFGBAR, &cfg);
    }

    return rc ? 0 : next;
}

static int virtio_pci_get_hfeatures(VDev *vdev, uint64_t *features)
{
    uint32_t feat0, feat1;
    int rc;

    rc = pci_bswap32_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_DFSELECT,
                           c_cap.bar, 0);

    rc |= pci_read_bswap32(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_DF,
                           c_cap.bar, &feat0);

    rc |= pci_bswap32_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_DFSELECT,
                            c_cap.bar, 1);

    rc |= pci_read_bswap32(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_DF,
                               c_cap.bar, &feat1);

    if (rc) {
        puts("Failed to get PCI feature bits");
        return -EIO;
    }

    *features = (uint64_t) feat1 << 32;
    *features |= (uint64_t) feat0;

    return 0;
}

static int virtio_pci_set_gfeatures(VDev *vdev)
{
    int rc;

    rc = pci_bswap32_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_GFSELECT,
                           c_cap.bar, 0);

    rc |= pci_bswap32_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_GF,
                            c_cap.bar, vdev->guest_features[1]);

    rc |= pci_bswap32_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_GFSELECT,
                            c_cap.bar, 1);

    rc |= pci_bswap32_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_GF,
                                c_cap.bar, vdev->guest_features[0]);

    if (rc) {
        puts("Failed to set PCI feature bits");
        return -EIO;
    }

    return 0;
}

static int virtio_pci_get_blk_config(VDev *vdev)
{
    return pci_read_flex(vdev->pci_fh, d_cap.off, d_cap.bar, &vdev->config.blk,
                         sizeof(VirtioBlkConfig));
}

/* virtio spec v1.3 section 4.1.2.1 */
void virtio_pci_id2type(VDev *vdev, uint16_t device_id)
{
    switch (device_id) {
    case 0x1001:
        vdev->dev_type = VIRTIO_ID_BLOCK;
        break;
    case 0x1000: /* Other valid but currently unsupported virtio device types */
    case 0x1004:
    default:
        vdev->dev_type = 0;
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
    int rc;

    /* Common capabilities */
    pos = find_cap_pos(vdev->pci_fh, VPCI_CAP_COMMON_CFG);
    if (!pos) {
        puts("Failed to locate PCI common configuration");
        return 1;
    }

    rc = pci_read_byte(vdev->pci_fh, pos + VPCI_CAP_BAR, PCI_CFGBAR, &c_cap.bar);
    if (rc || pci_read_bswap32(vdev->pci_fh, pos + VPCI_CAP_OFFSET, PCI_CFGBAR,
                               &c_cap.off)) {
        puts("Failed to read PCI common configuration");
        return -EIO;
    }

    /* Device capabilities */
    pos = find_cap_pos(vdev->pci_fh, VPCI_CAP_DEVICE_CFG);
    if (!pos) {
        puts("Failed to locate PCI device configuration");
        return 1;
    }

    rc = pci_read_byte(vdev->pci_fh, pos + VPCI_CAP_BAR, PCI_CFGBAR, &d_cap.bar);
    if (rc || pci_read_bswap32(vdev->pci_fh, pos + VPCI_CAP_OFFSET, PCI_CFGBAR,
                               &d_cap.off)) {
        puts("Failed to read PCI device configuration");
        return -EIO;
    }

    /* Notification capabilities */
    pos = find_cap_pos(vdev->pci_fh, VPCI_CAP_NOTIFY_CFG);
    if (!pos) {
        puts("Failed to locate PCI notification configuration");
        return 1;
    }

    rc = pci_read_byte(vdev->pci_fh, pos + VPCI_CAP_BAR, PCI_CFGBAR, &n_cap.bar);
    if (rc || pci_read_bswap32(vdev->pci_fh, pos + VPCI_CAP_OFFSET, PCI_CFGBAR,
                               &n_cap.off)) {
        puts("Failed to read PCI notification configuration");
        return -EIO;
    }

    rc = pci_read_bswap32(vdev->pci_fh, pos + VPCI_N_CAP_MULT, PCI_CFGBAR, &notify_mult);
    if (rc || pci_read_bswap16(vdev->pci_fh, d_cap.off + VPCI_C_OFFSET_Q_NOFF,
                               d_cap.bar, &q_notify_offset)) {
        puts("Failed to read notification queue configuration");
        return -EIO;
    }

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

int virtio_pci_set_selected_vq(VDev *vdev, uint16_t queue_num)
{
    return pci_bswap16_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_Q_SELECT,
                             c_cap.bar, queue_num);
}

int virtio_pci_set_queue_size(VDev *vdev, uint16_t queue_size)
{
    return pci_bswap16_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_Q_SIZE,
                             c_cap.bar, queue_size);
}

static int virtio_pci_set_queue_enable(VDev *vdev, uint16_t enabled)
{
    return pci_bswap16_write(vdev->pci_fh, c_cap.off + VPCI_C_OFFSET_Q_ENABLE,
                             c_cap.bar, enabled);
}

static int set_pci_vq_addr(VDev *vdev, uint64_t config_off, void *addr)
{
    return pci_bswap64_write(vdev->pci_fh, c_cap.off + config_off, c_cap.bar,
                             (uint64_t) addr);
}

int virtio_pci_setup(VDev *vdev)
{
    VRing *vr;
    int rc;
    uint64_t pci_features;
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

    rc = virtio_pci_read_pci_cap_config(vdev);
    if (rc) {
        printf("Invalid PCI capabilities");
        return -EIO;
    }

    switch (vdev->dev_type) {
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
    if (rc || virtio_pci_set_gfeatures(vdev)) {
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

        if (pci_read_flex(vdev->pci_fh, VPCI_C_COMMON_NUMQ, c_cap.bar, &info.num, 2)) {
            return -EIO;
        }

        vring_init(vr, &info);

        if (virtio_pci_set_selected_vq(vdev, vr->id)) {
            puts("Failed to set selected virt-queue");
            return -EIO;
        }

        if (virtio_pci_set_queue_size(vdev, VIRTIO_RING_SIZE)) {
            puts("Failed to set virt-queue size");
            return -EIO;
        }

        rc = set_pci_vq_addr(vdev, VPCI_C_OFFSET_Q_DESCLO, vr->desc);
        rc |= set_pci_vq_addr(vdev, VPCI_C_OFFSET_Q_AVAILLO, vr->avail);
        rc |= set_pci_vq_addr(vdev, VPCI_C_OFFSET_Q_USEDLO, vr->used);
        if (rc) {
            puts("Failed to configure virt-queue address");
            return -EIO;
        }

        if (virtio_pci_set_queue_enable(vdev, true)) {
            puts("Failed to set virt-queue enabled");
            return -EIO;
        }
    }

    status |= VPCI_S_FEATURES_OK | VPCI_S_DRIVER_OK;
    return virtio_pci_set_status(vdev, status);
}

int virtio_pci_setup_device(void)
{
    VDev *vdev = virtio_get_device();

    if (enable_pci_function(&vdev->pci_fh)) {
        puts("Failed to enable PCI function");
        return -ENODEV;
    }

    return 0;
}

long virtio_pci_notify(uint32_t fhandle, int vq_id)
{
    uint32_t offset = n_cap.off + notify_mult * q_notify_offset;
    return pci_bswap16_write(fhandle, offset, n_cap.bar, (uint16_t) vq_id);
}
