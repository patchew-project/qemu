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
#include "s390-pci-bus.h"
#include "s390-pci-clp.h"
#include "s390-pci-vfio.h"
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

static void *get_next_clp_buf(struct vfio_region_zpci_info *zpci_info,
                              struct vfio_region_zpci_info_hdr *hdr)
{
    /* If the next payload would be beyond the region, we're done */
    if (zpci_info->argsz <= hdr->next) {
        return NULL;
    }

    return (void *)zpci_info + hdr->next;
}

static void *find_clp_data(struct vfio_region_zpci_info *zpci_info, int id)
{
    struct vfio_region_zpci_info_hdr *hdr;
    void *clp;

    assert(zpci_info);

    /* Jump to the first CLP feature, which starts with header information */
    clp = (void *)zpci_info + zpci_info->offset;
    hdr = (struct vfio_region_zpci_info_hdr *)clp;

    while (hdr != NULL) {
        if (hdr->id == id) {
            return hdr;
        }
        hdr = get_next_clp_buf(zpci_info, hdr);
    }

    return NULL;
}

static void s390_pci_read_qpci(S390PCIBusDevice *pbdev,
                               struct vfio_region_zpci_info *zpci_info)
{
    struct vfio_region_zpci_info_qpci *clp;

    clp = find_clp_data(zpci_info, VFIO_REGION_ZPCI_INFO_QPCI);

    /* If CLP feature not provided, just leave the defaults in place */
    if (clp == NULL) {
        DPRINTF("QPCI clp feature not found\n");
        return;
    }

    pbdev->zpci_fn.sdma = clp->start_dma;
    pbdev->zpci_fn.edma = clp->end_dma;
    pbdev->zpci_fn.pchid = clp->pchid;
    pbdev->zpci_fn.vfn = clp->vfn;
    pbdev->zpci_fn.pfgid = clp->gid;
    /* The following values remain 0 until we support other FMB formats */
    pbdev->zpci_fn.fmbl = 0;
    pbdev->zpci_fn.pft = 0;
}

static void s390_pci_read_qpcifg(S390PCIBusDevice *pbdev,
                                 struct vfio_region_zpci_info *zpci_info)
{
    struct vfio_region_zpci_info_qpcifg *clp;
    ClpRspQueryPciGrp *resgrp;

    clp = find_clp_data(zpci_info, VFIO_REGION_ZPCI_INFO_QPCIFG);

    /* If CLP feature not provided, just use the default group */
    if (clp == NULL) {
        DPRINTF("QPCIFG clp feature not found\n");
        pbdev->zpci_fn.pfgid = ZPCI_DEFAULT_FN_GRP;
        pbdev->pci_grp = s390_grp_find(ZPCI_DEFAULT_FN_GRP);
        return;
    }

    /* See if the PCI group is already defined, create if not */
    pbdev->pci_grp = s390_grp_find(pbdev->zpci_fn.pfgid);

    if (!pbdev->pci_grp) {
        pbdev->pci_grp = s390_grp_create(pbdev->zpci_fn.pfgid);

        resgrp = &pbdev->pci_grp->zpci_grp;
        if (clp->flags & VFIO_PCI_ZDEV_FLAGS_REFRESH) {
            resgrp->fr = 1;
        }
        stq_p(&resgrp->dasm, clp->dasm);
        stq_p(&resgrp->msia, clp->msi_addr);
        stw_p(&resgrp->mui, clp->mui);
        stw_p(&resgrp->i, clp->noi);
        stw_p(&resgrp->maxstbl, clp->maxstbl);
        stb_p(&resgrp->version, clp->version);
    }
}

static void s390_pci_read_util(S390PCIBusDevice *pbdev,
                               struct vfio_region_zpci_info *zpci_info)
{
    struct vfio_region_zpci_info_util *clp;

    clp = find_clp_data(zpci_info, VFIO_REGION_ZPCI_INFO_UTIL);

    /* If CLP feature not provided or unusable, leave the defaults in place */
    if (clp == NULL) {
        DPRINTF("UTIL clp feature not found\n");
        return;
    }
    if (clp->size > CLP_UTIL_STR_LEN) {
        DPRINTF("UTIL clp feature unexpected size\n");
        return;
    }

    pbdev->zpci_fn.flags |= CLP_RSP_QPCI_MASK_UTIL;
    memcpy(pbdev->zpci_fn.util_str, clp->util_str, CLP_UTIL_STR_LEN);
}

static void s390_pci_read_pfip(S390PCIBusDevice *pbdev,
                               struct vfio_region_zpci_info *zpci_info)
{
    struct vfio_region_zpci_info_pfip *clp;

    clp = find_clp_data(zpci_info, VFIO_REGION_ZPCI_INFO_PFIP);

    /* If CLP feature not provided or unusable, leave the defaults in place */
    if (clp == NULL) {
        DPRINTF("PFIP clp feature not found\n");
        return;
    }
    if (clp->size > CLP_PFIP_NR_SEGMENTS) {
        DPRINTF("PFIP clp feature unexpected size\n");
        return;
    }

    memcpy(pbdev->zpci_fn.pfip, clp->pfip, CLP_PFIP_NR_SEGMENTS);
}

/*
 * This function will look for the VFIO_REGION_SUBTYPE_IBM_ZPCI_CLP vfio device
 * region, which has information about CLP features provided by the underlying
 * host.  On entry, defaults have already been placed into the guest CLP
 * response buffers.  On exit, defaults will have been overwritten for any CLP
 * features found in the region; defaults will remain for any CLP features not
 * found in the region.
 */
void s390_pci_get_clp_info(S390PCIBusDevice *pbdev)
{
    VFIOPCIDevice *vfio_pci;
    VFIODevice *vdev;
    struct vfio_region_info *info;
    struct vfio_region_zpci_info *zpci_info;
    int size, argsz;
    int ret;

    vfio_pci = container_of(pbdev->pdev, VFIOPCIDevice, pdev);
    vdev = &vfio_pci->vbasedev;

    if (vdev->num_regions < VFIO_PCI_NUM_REGIONS + 1) {
        /* Fall back to old handling */
        DPRINTF("No zPCI vfio region available\n");
        return;
    }

    ret = vfio_get_dev_region_info(vdev,
                                   PCI_VENDOR_ID_IBM |
                                   VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
                                   VFIO_REGION_SUBTYPE_IBM_ZPCI_CLP, &info);
    if (ret) {
        /* Fall back to old handling */
        DPRINTF("zPCI vfio region not found\n");
        return;
    }

    /* Start by determining the region size */
    zpci_info = g_malloc(sizeof(*zpci_info));
    size = pread(vdev->fd, zpci_info, sizeof(*zpci_info), info->offset);
    if (size != sizeof(*zpci_info)) {
        DPRINTF("Failed to read vfio zPCI device region header\n");
        goto end;
    }

    /* Allocate a buffer for the entire region */
    argsz = zpci_info->argsz;
    zpci_info = g_realloc(zpci_info, argsz);

    /* Read the entire region now */
    size = pread(vdev->fd, zpci_info, argsz, info->offset);
    if (size != argsz) {
        DPRINTF("Failed to read vfio zPCI device region\n");
        goto end;
    }

    /*
     * Find the CLP features provided and fill in the guest CLP responses.
     * Always call s390_pci_read_qpci first as information from this could
     * determine which function group is used in s390_pci_read_qpcifg.
     * For any feature not found, the default values will remain in the CLP
     * response.
     */
    s390_pci_read_qpci(pbdev, zpci_info);
    s390_pci_read_qpcifg(pbdev, zpci_info);
    s390_pci_read_util(pbdev, zpci_info);
    s390_pci_read_pfip(pbdev, zpci_info);

end:
    g_free(info);
    g_free(zpci_info);
    return;
}
