/*
 * s390 vfio-pci interfaces
 *
 * Copyright 2020 IBM Corp.
 * Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <sys/ioctl.h>
#include <linux/vfio.h>
#include <linux/vfio_zdev.h>

#include "qemu/osdep.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/s390-pci-clp.h"
#include "hw/s390x/s390-pci-vfio.h"
#include "hw/vfio/pci.h"

#ifndef DEBUG_S390PCI_VFIO
#define DEBUG_S390PCI_VFIO  0
#endif

#define DPRINTF(fmt, ...)                                          \
    do {                                                           \
        if (DEBUG_S390PCI_VFIO) {                                  \
            fprintf(stderr, "S390pci-vfio: " fmt, ## __VA_ARGS__); \
        }                                                          \
    } while (0)

static void s390_pci_read_base(S390PCIBusDevice *pbdev,
                               struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_base *cap;

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_BASE);

    /* If capability not provided, just leave the defaults in place */
    if (hdr == NULL) {
        DPRINTF("Base PCI clp capability not found\n");
        return;
    }
    cap = (void *) hdr;

    pbdev->zpci_fn.sdma = cap->start_dma;
    pbdev->zpci_fn.edma = cap->end_dma;
    pbdev->zpci_fn.pchid = cap->pchid;
    pbdev->zpci_fn.vfn = cap->vfn;
    pbdev->zpci_fn.pfgid = cap->gid;
    /* The following values remain 0 until we support other FMB formats */
    pbdev->zpci_fn.fmbl = 0;
    pbdev->zpci_fn.pft = 0;
}

static void s390_pci_read_group(S390PCIBusDevice *pbdev,
                                struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_group *cap;
    ClpRspQueryPciGrp *resgrp;

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_GROUP);

    /* If capability not provided, just use the default group */
    if (hdr == NULL) {
        DPRINTF("Base PCI Group clp capability not found\n");
        pbdev->zpci_fn.pfgid = ZPCI_DEFAULT_FN_GRP;
        pbdev->pci_group = s390_group_find(ZPCI_DEFAULT_FN_GRP);
        return;
    }
    cap = (void *) hdr;

    /* See if the PCI group is already defined, create if not */
    pbdev->pci_group = s390_group_find(pbdev->zpci_fn.pfgid);

    if (!pbdev->pci_group) {
        pbdev->pci_group = s390_group_create(pbdev->zpci_fn.pfgid);

        resgrp = &pbdev->pci_group->zpci_group;
        if (cap->flags & VFIO_DEVICE_INFO_ZPCI_FLAG_REFRESH) {
            resgrp->fr = 1;
        }
        stq_p(&resgrp->dasm, cap->dasm);
        stq_p(&resgrp->msia, cap->msi_addr);
        stw_p(&resgrp->mui, cap->mui);
        stw_p(&resgrp->i, cap->noi);
        stw_p(&resgrp->maxstbl, cap->maxstbl);
        stb_p(&resgrp->version, cap->version);
    }
}

static void s390_pci_read_util(S390PCIBusDevice *pbdev,
                               struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_util *cap;

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_UTIL);

    /* If capability not provided, just leave the defaults in place */
    if (hdr == NULL) {
        DPRINTF("Util clp capability not found\n");
        return;
    }
    cap = (void *) hdr;

    if (cap->size > CLP_UTIL_STR_LEN) {
        DPRINTF("UTIL clp capability unexpected size\n");
        return;
    }

    pbdev->zpci_fn.flags |= CLP_RSP_QPCI_MASK_UTIL;
    memcpy(pbdev->zpci_fn.util_str, cap->util_str, CLP_UTIL_STR_LEN);
}

static void s390_pci_read_pfip(S390PCIBusDevice *pbdev,
                               struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_pfip *cap;

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_PFIP);

    /* If capability not provided, just leave the defaults in place */
    if (hdr == NULL) {
        DPRINTF("PFIP clp capability not found\n");
        return;
    }
    cap = (void *) hdr;

    if (cap->size > CLP_PFIP_NR_SEGMENTS) {
        DPRINTF("PFIP clp capability unexpected size\n");
        return;
    }

    memcpy(pbdev->zpci_fn.pfip, cap->pfip, CLP_PFIP_NR_SEGMENTS);
}

/*
 * This function will issue the VFIO_DEVICE_GET_INFO ioctl and look for
 * capabilities that contain information about CLP features provided by the
 * underlying host.
 * On entry, defaults have already been placed into the guest CLP response
 * buffers.  On exit, defaults will have been overwritten for any CLP features
 * found in the capability chain; defaults will remain for any CLP features not
 * found in the chain.
 */
void s390_pci_get_clp_info(S390PCIBusDevice *pbdev)
{
    g_autofree struct vfio_device_info *info;
    VFIOPCIDevice *vfio_pci;
    uint32_t argsz;
    int fd;

    argsz = sizeof(*info);
    info = g_malloc0(argsz);

    vfio_pci = container_of(pbdev->pdev, VFIOPCIDevice, pdev);
    fd = vfio_pci->vbasedev.fd;

    /*
     * If the specified argsz is not large enough to contain all capabilities
     * it will be updated upon return from the ioctl.  Retry until we have
     * a big enough buffer to hold the entire capability chain.  On error,
     * just exit and rely on CLP defaults.
     */
retry:
    info->argsz = argsz;

    if (ioctl(fd, VFIO_DEVICE_GET_INFO, info)) {
        DPRINTF("zPCI could not read vfio device info\n");
        return;
    }

    if (info->argsz > argsz) {
        argsz = info->argsz;
        info = g_realloc(info, argsz);
        goto retry;
    }

    /*
     * Find the CLP features provided and fill in the guest CLP responses.
     * Always call s390_pci_read_base first as information from this could
     * determine which function group is used in s390_pci_read_group.
     * For any feature not found, the default values will remain in the CLP
     * response.
     */
    s390_pci_read_base(pbdev, info);
    s390_pci_read_group(pbdev, info);
    s390_pci_read_util(pbdev, info);
    s390_pci_read_pfip(pbdev, info);

    return;
}
