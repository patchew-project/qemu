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
#include "trace.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/s390-pci-clp.h"
#include "hw/s390x/s390-pci-inst.h"
#include "hw/s390x/s390-pci-vfio.h"
#include "hw/vfio/pci.h"
#include "hw/vfio/vfio-common.h"

/*
 * Get the current DMA available count from vfio.  Returns true if vfio is
 * limiting DMA requests, false otherwise.  The current available count read
 * from vfio is returned in avail.
 */
bool s390_pci_update_dma_avail(int fd, unsigned int *avail)
{
    g_autofree struct vfio_iommu_type1_info *info;
    uint32_t argsz;

    assert(avail);

    argsz = sizeof(struct vfio_iommu_type1_info);
    info = g_malloc0(argsz);

    /*
     * If the specified argsz is not large enough to contain all capabilities
     * it will be updated upon return from the ioctl.  Retry until we have
     * a big enough buffer to hold the entire capability chain.
     */
retry:
    info->argsz = argsz;

    if (ioctl(fd, VFIO_IOMMU_GET_INFO, info)) {
        return false;
    }

    if (info->argsz > argsz) {
        argsz = info->argsz;
        info = g_realloc(info, argsz);
        goto retry;
    }

    /* If the capability exists, update with the current value */
    return vfio_get_info_dma_avail(info, avail);
}

S390PCIDMACount *s390_pci_start_dma_count(S390pciState *s,
                                          S390PCIBusDevice *pbdev)
{
    S390PCIDMACount *cnt;
    uint32_t avail;
    VFIOPCIDevice *vpdev = container_of(pbdev->pdev, VFIOPCIDevice, pdev);
    int id;

    assert(vpdev);

    id = vpdev->vbasedev.group->container->fd;

    if (!s390_pci_update_dma_avail(id, &avail)) {
        return NULL;
    }

    QTAILQ_FOREACH(cnt, &s->zpci_dma_limit, link) {
        if (cnt->id  == id) {
            cnt->users++;
            return cnt;
        }
    }

    cnt = g_new0(S390PCIDMACount, 1);
    cnt->id = id;
    cnt->users = 1;
    cnt->avail = avail;
    QTAILQ_INSERT_TAIL(&s->zpci_dma_limit, cnt, link);
    return cnt;
}

void s390_pci_end_dma_count(S390pciState *s, S390PCIDMACount *cnt)
{
    assert(cnt);

    cnt->users--;
    if (cnt->users == 0) {
        QTAILQ_REMOVE(&s->zpci_dma_limit, cnt, link);
    }
}

static void s390_pci_read_base(S390PCIBusDevice *pbdev,
                               struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_base *cap;
    VFIOPCIDevice *vpci =  container_of(pbdev->pdev, VFIOPCIDevice, pdev);

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_BASE);

    /* If capability not provided, just leave the defaults in place */
    if (hdr == NULL) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_BASE);
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
    VFIOPCIDevice *vpci =  container_of(pbdev->pdev, VFIOPCIDevice, pdev);

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_GROUP);

    /* If capability not provided, just use the default group */
    if (hdr == NULL) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_GROUP);
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
        if (cap->flags & VFIO_DEVICE_INFO_ZPCI_FLAG_RELAXED) {
            resgrp->fr |= CLP_RSP_QPCIG_MASK_RELAXED;
        }
        resgrp->dasm = cap->dasm;
        resgrp->msia = cap->msi_addr;
        resgrp->mui = cap->mui;
        resgrp->i = cap->noi;
        resgrp->maxstbl = cap->maxstbl;
        resgrp->version = cap->version;
    }
}

static void s390_pci_read_util(S390PCIBusDevice *pbdev,
                               struct vfio_device_info *info)
{
    struct vfio_info_cap_header *hdr;
    struct vfio_device_info_cap_zpci_util *cap;
    VFIOPCIDevice *vpci =  container_of(pbdev->pdev, VFIOPCIDevice, pdev);

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_UTIL);

    /* If capability not provided, just leave the defaults in place */
    if (hdr == NULL) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_UTIL);
        return;
    }
    cap = (void *) hdr;

    if (cap->size > CLP_UTIL_STR_LEN) {
        trace_s390_pci_clp_cap_size(vpci->vbasedev.name, cap->size,
                                    VFIO_DEVICE_INFO_CAP_ZPCI_UTIL);
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
    VFIOPCIDevice *vpci =  container_of(pbdev->pdev, VFIOPCIDevice, pdev);

    hdr = vfio_get_device_info_cap(info, VFIO_DEVICE_INFO_CAP_ZPCI_PFIP);

    /* If capability not provided, just leave the defaults in place */
    if (hdr == NULL) {
        trace_s390_pci_clp_cap(vpci->vbasedev.name,
                               VFIO_DEVICE_INFO_CAP_ZPCI_PFIP);
        return;
    }
    cap = (void *) hdr;

    if (cap->size > CLP_PFIP_NR_SEGMENTS) {
        trace_s390_pci_clp_cap_size(vpci->vbasedev.name, cap->size,
                                    VFIO_DEVICE_INFO_CAP_ZPCI_PFIP);
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
        trace_s390_pci_clp_dev_info(vfio_pci->vbasedev.name);
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

/*
 * This function will look for the VFIO_REGION_SUBTYPE_IBM_ZPCI_IO vfio
 * device region, which is used for performing block I/O operations.
 */
int s390_pci_get_zpci_io_region(S390PCIBusDevice *pbdev)
{
    VFIOPCIDevice *vfio_pci;
    VFIODevice *vdev;
    struct vfio_region_info *info;
    int ret;

    vfio_pci = container_of(pbdev->pdev, VFIOPCIDevice, pdev);
    vdev = &vfio_pci->vbasedev;

    if (vdev->num_regions < VFIO_PCI_NUM_REGIONS + 1) {
        return -ENOENT;
    }

    /* Get the I/O region if it's available */
    if (vfio_get_dev_region_info(vdev,
                                 PCI_VENDOR_ID_IBM |
                                 VFIO_REGION_TYPE_PCI_VENDOR_TYPE,
                                 VFIO_REGION_SUBTYPE_IBM_ZPCI_IO, &info)) {
        return -ENOENT;
    }

    /* If the size is unexpectedly small, don't use the region */
    if (sizeof(*pbdev->io_region) > info->size) {
        return -EINVAL;
    }

    pbdev->io_region = g_malloc0(info->size);

    /* Check the header for setup information */
    ret = pread(vfio_pci->vbasedev.fd, &pbdev->io_region->hdr,
                sizeof(struct vfio_zpci_io_hdr), info->offset);
    if (ret != sizeof(struct vfio_zpci_io_hdr)) {
        g_free(pbdev->io_region);
        pbdev->io_region = 0;
        ret = -EINVAL;
    } else {
        pbdev->io_region_op_offset = info->offset +
                                     offsetof(struct vfio_region_zpci_io, req);
        /* All devices in this group will use the I/O region for PCISTB */
        pbdev->pci_group->zpci_group.maxstbl = MIN(PAGE_SIZE,
                                               pbdev->io_region->hdr.max);
        ret = 0;
    }
    g_free(info);

    /* Register the new handlers for the device if region available */
    if (pbdev->io_region) {
        zpci_assign_ops_vfio_io_region(pbdev);
    }

    return ret;
}

int s390_pci_vfio_pcistb(S390PCIBusDevice *pbdev, S390CPU *cpu, uint64_t gaddr,
                         uint8_t ar, uint8_t pcias, uint16_t len,
                         uint64_t offset)
{
    struct vfio_region_zpci_io *region = pbdev->io_region;
    VFIOPCIDevice *vfio_pci;
    uint8_t *buffer;
    int ret;

    if (region == NULL) {
        return -EIO;
    }

    vfio_pci = container_of(pbdev->pdev, VFIOPCIDevice, pdev);

    /*
     * We've already ensured the input can be no larger than a page.  PCISTB
     * requires that the operation payload does not cross a page boundary,
     * otherwise the operation will be rejected.  Therefore, just get a single
     * page for the write.
     */
    buffer = qemu_memalign(PAGE_SIZE, PAGE_SIZE);

    if (s390_cpu_virt_mem_read(cpu, gaddr, ar, buffer, len)) {
        ret = -EACCES;
        goto out;
    }

    region->req.gaddr = (uint64_t)buffer;
    region->req.offset = offset;
    region->req.len = len;
    region->req.pcias = pcias;
    region->req.flags = VFIO_ZPCI_IO_FLAG_BLOCKW;

    ret = pwrite(vfio_pci->vbasedev.fd, &region->req,
                 sizeof(struct vfio_zpci_io_req),
                 pbdev->io_region_op_offset);
    if (ret != sizeof(struct vfio_zpci_io_req)) {
        ret = -EIO;
    } else {
        ret = 0;
    }

out:
    qemu_vfree(buffer);

    return ret;
}
