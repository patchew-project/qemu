/*
 * VFIO region
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_REGION_H
#define HW_VFIO_REGION_H

#include "system/memory.h"

typedef struct VFIOMmap {
    MemoryRegion mem;
    void *mmap;
    off_t offset;       /* offset within the region */
    size_t size;
    int fd;             /* per-mmap fd, or -1 to use the region fd */
    uint64_t fd_offset; /* offset within fd for the mmap call */
} VFIOMmap;

typedef struct VFIODevice VFIODevice;

typedef struct VFIORegion {
    struct VFIODevice *vbasedev;
    off_t fd_offset; /* offset of region within device fd */
    MemoryRegion *mem; /* slow, read/write access */
    size_t size;
    uint32_t flags; /* VFIO region flags (rd/wr/mmap) */
    uint32_t nr_mmaps;
    VFIOMmap *mmaps;
    uint8_t nr; /* cache the region number for debug */
    bool post_wr; /* writes can be posted */
} VFIORegion;


void vfio_region_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned size);
uint64_t vfio_region_read(void *opaque,
                          hwaddr addr, unsigned size);
int vfio_region_setup(Object *obj, VFIODevice *vbasedev, VFIORegion *region,
                      int index, const char *name, Error **errp);
int vfio_region_mmap(VFIORegion *region);
void vfio_region_exit(VFIORegion *region);
void vfio_region_finalize(VFIORegion *region);

struct vfio_region_info;
int vfio_region_setup_sparse_mmaps(VFIORegion *region,
                                    struct vfio_region_info *info,
                                    Error **errp);

struct VFIOPCIDevice;
void vfio_region_set_enabled(struct VFIOPCIDevice *vdev, bool enabled);

#endif /* HW_VFIO_REGION_H */
