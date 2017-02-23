/*
 * NVMe VFIO interface
 *
 * Copyright 2016 Red Hat, Inc.
 *
 * Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "qapi/error.h"
#include "exec/ramlist.h"
#include "trace.h"
#include "qemu/queue.h"
#include "qemu/error-report.h"
#include "standard-headers/linux/pci_regs.h"
#include "qemu/event_notifier.h"
#include "block/nvme-vfio.h"
#include "trace.h"

#define NVME_DEBUG 0

#define NVME_VFIO_IOVA_MIN 0x10000ULL
/* XXX: Once VFIO exposes the iova bit width in the IOMMU capability interface,
 * we can use a runtime limit. (alternatively it's also possible to do platform
 * specific detection by reading sysfs entries). Until then, 39 is a safe bet.
 **/
#define NVME_VFIO_IOVA_MAX (1ULL << 39)

typedef struct {
    /* Page aligned. */
    void *host;
    size_t size;
    uint64_t iova;
} IOVAMapping;

struct NVMeVFIOState {
    int container;
    int group;
    int device;
    RAMBlockNotifier ram_notifier;
    struct vfio_region_info config_region_info, bar_region_info[6];

    /* VFIO's IO virtual address space is managed by splitting into a few
     * sections:
     *
     * ---------------       <= 0
     * |xxxxxxxxxxxxx|
     * |-------------|       <= NVME_VFIO_IOVA_MIN
     * |             |
     * |    Fixed    |
     * |             |
     * |-------------|       <= low_water_mark
     * |             |
     * |    Free     |
     * |             |
     * |-------------|       <= high_water_mark
     * |             |
     * |    Temp     |
     * |             |
     * |-------------|       <= NVME_VFIO_IOVA_MAX
     * |xxxxxxxxxxxxx|
     * |xxxxxxxxxxxxx|
     * ---------------
     *
     * - Addresses lower than NVME_VFIO_IOVA_MIN are reserved to distinguish
     *   null IOVAs;
     *
     * - Fixed mappings from host virtual addresses to IO virtual addresses are
     *   assigned "low" IOVAs in the range of [NVME_VFIO_IOVA_MIN,
     *   low_water_mark).  Once allocated they will not be reclaimed -
     *   low_water_mark never shrinks;
     *
     * - IOVAs in range [low_water_mark, high_water_mark) are unused;
     *
     * - IOVAs in range [high_water_mark, NVME_VFIO_IOVA_MAX) are volatile
     *   mappings. At each nvme_vfio_dma_reset_temporary() call, the whole area
     *   is recycled. The caller should make sure I/O's depending on these
     *   mappings are released.
     **/
    uint64_t low_water_mark;
    uint64_t high_water_mark;
    IOVAMapping *mappings;
    int nr_mappings;
};

static int sysfs_find_group_file(const char *device, char **path, Error **errp)
{
    int ret;
    char *sysfs_link = NULL;
    char *sysfs_group = NULL;
    char *p;

    sysfs_link = g_strdup_printf("/sys/bus/pci/devices/%s/iommu_group",
                                 device);
    sysfs_group = g_malloc(PATH_MAX);
    ret = readlink(sysfs_link, sysfs_group, PATH_MAX - 1);
    if (ret == -1) {
        error_setg_errno(errp, errno, "Failed to find iommu group sysfs path");
        ret = -errno;
        goto out;
    }
    ret = 0;
    p = strrchr(sysfs_group, '/');
    if (!p) {
        error_setg(errp, "Failed to find iommu group number");
        ret = -errno;
        goto out;
    }

    *path = g_strdup_printf("/dev/vfio/%s", p + 1);
out:
    g_free(sysfs_link);
    g_free(sysfs_group);
    return ret;
}

static int nvme_vfio_pci_init_bar(NVMeVFIOState *s, unsigned int index,
                                  Error **errp)
{
    assert(index < ARRAY_SIZE(s->bar_region_info));
    s->bar_region_info[index] = (struct vfio_region_info) {
        .index = VFIO_PCI_BAR0_REGION_INDEX + index,
        .argsz = sizeof(struct vfio_region_info),
    };
    if (ioctl(s->device, VFIO_DEVICE_GET_REGION_INFO, &s->bar_region_info[index])) {
        error_setg_errno(errp, errno, "Failed to get BAR region info");
        return -errno;
    }

    return 0;
}

/**
 * Map a PCI bar area.
 */
void *nvme_vfio_pci_map_bar(NVMeVFIOState *s, int index, Error **errp)
{
    void *p;
    assert(index >= 0 && index < 6);
    p = mmap(NULL, MIN(8192, s->bar_region_info[index].size),
             PROT_READ | PROT_WRITE, MAP_SHARED,
             s->device, s->bar_region_info[index].offset);
    if (p == MAP_FAILED) {
        error_setg_errno(errp, errno, "Failed to map BAR region");
        p = NULL;
    }
    return p;
}

/**
 * Unmap a PCI bar area.
 */
void nvme_vfio_pci_unmap_bar(NVMeVFIOState *s, int index, void *bar)
{
    if (bar) {
        munmap(bar, MIN(8192, s->bar_region_info[index].size));
    }
}

/**
 * Initialize device IRQ with @irq_type and and register an event notifier.
 */
int nvme_vfio_pci_init_irq(NVMeVFIOState *s, EventNotifier *e,
                           int irq_type, Error **errp)
{
    int r;
    struct vfio_irq_set *irq_set;
    size_t irq_set_size;
    struct vfio_irq_info irq_info = { .argsz = sizeof(irq_info) };

    irq_info.index = irq_type;
    if (ioctl(s->device, VFIO_DEVICE_GET_IRQ_INFO, &irq_info)) {
        error_setg_errno(errp, errno, "Failed to get device interrupt info");
        return -errno;
    }
    if (!(irq_info.flags & VFIO_IRQ_INFO_EVENTFD)) {
        error_setg(errp, "Device interrupt doesn't support eventfd");
        return -EINVAL;
    }

    irq_set_size = sizeof(*irq_set) + sizeof(int);
    irq_set = g_malloc0(irq_set_size);

    /* Get to a known IRQ state */
    *irq_set = (struct vfio_irq_set) {
        .argsz = irq_set_size,
        .flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = irq_info.index,
        .start = 0,
        .count = 1,
    };

    *(int *)&irq_set->data = event_notifier_get_fd(e);
    r = ioctl(s->device, VFIO_DEVICE_SET_IRQS, irq_set);
    g_free(irq_set);
    if (r) {
        error_setg_errno(errp, errno, "Failed to setup device interrupt");
        return -errno;
    }
    return 0;
}

static int nvme_vfio_pci_read_config(NVMeVFIOState *s, void *buf, int size, int ofs)
{
    if (pread(s->device, buf, size, s->config_region_info.offset + ofs) == size) {
        return 0;
    }
    return -1;
}

static int nvme_vfio_pci_write_config(NVMeVFIOState *s, void *buf, int size, int ofs)
{
    if (pwrite(s->device, buf, size,
               s->config_region_info.offset + ofs) == size) {
        return 0;
    }

    return -1;
}

static int nvme_vfio_init_pci(NVMeVFIOState *s, const char *device,
                              Error **errp)
{
    int ret;
    int i;
    uint16_t pci_cmd;
    struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
    struct vfio_iommu_type1_info iommu_info = { .argsz = sizeof(iommu_info) };
    struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
    char *group_file = NULL;

    /* Create a new container */
    s->container = open("/dev/vfio/vfio", O_RDWR);

    if (ioctl(s->container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        error_setg(errp, "Invalid VFIO version");
        ret = -EINVAL;
        goto out;
    }

    if (!ioctl(s->container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        error_setg_errno(errp, errno, "VFIO IOMMU check failed");
        ret = -EINVAL;
        goto out;
    }

    /* Open the group */
    ret = sysfs_find_group_file(device, &group_file, errp);
    if (ret) {
        goto out;
    }

    s->group = open(group_file, O_RDWR);
    g_free(group_file);
    if (s->group <= 0) {
        error_setg_errno(errp, errno, "Failed to open VFIO group file");
        ret = -errno;
        goto out;
    }

    /* Test the group is viable and available */
    if (ioctl(s->group, VFIO_GROUP_GET_STATUS, &group_status)) {
        error_setg_errno(errp, errno, "Failed to get VFIO group status");
        ret = -errno;
        goto out;
    }

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        error_setg(errp, "VFIO group is not viable");
        ret = -EINVAL;
        goto out;
    }

    /* Add the group to the container */
    if (ioctl(s->group, VFIO_GROUP_SET_CONTAINER, &s->container)) {
        error_setg_errno(errp, errno, "Failed to add group to VFIO container");
        ret = -errno;
        goto out;
    }

    /* Enable the IOMMU model we want */
    if (ioctl(s->container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU)) {
        error_setg_errno(errp, errno, "Failed to set VFIO IOMMU type");
        ret = -errno;
        goto out;
    }

    /* Get additional IOMMU info */
    if (ioctl(s->container, VFIO_IOMMU_GET_INFO, &iommu_info)) {
        error_setg_errno(errp, errno, "Failed to get IOMMU info");
        ret = -errno;
        goto out;
    }

    s->device = ioctl(s->group, VFIO_GROUP_GET_DEVICE_FD, device);

    if (s->device < 0) {
        error_setg_errno(errp, errno, "Failed to get device fd");
        ret = -errno;
        goto out;
    }

    /* Test and setup the device */
    if (ioctl(s->device, VFIO_DEVICE_GET_INFO, &device_info)) {
        error_setg_errno(errp, errno, "Failed to get device info");
        ret = -errno;
        goto out;
    }

    if (device_info.num_regions < VFIO_PCI_CONFIG_REGION_INDEX) {
        error_setg(errp, "Invalid device regions");
        ret = -EINVAL;
        goto out;
    }

    s->config_region_info = (struct vfio_region_info) {
        .index = VFIO_PCI_CONFIG_REGION_INDEX,
        .argsz = sizeof(struct vfio_region_info),
    };
    if (ioctl(s->device, VFIO_DEVICE_GET_REGION_INFO, &s->config_region_info)) {
        error_setg_errno(errp, errno, "Failed to get config region info");
        ret = -errno;
        goto out;
    }

    for (i = 0; i < 6; i++) {
        ret = nvme_vfio_pci_init_bar(s, i, errp);
        if (ret) {
            goto out;
        }
    }

    /* Enable bus master */
    if (nvme_vfio_pci_read_config(s, &pci_cmd, sizeof(pci_cmd),
                                  PCI_COMMAND) < 0) {
        goto out;
    }
    pci_cmd |= PCI_COMMAND_MASTER;
    if (nvme_vfio_pci_write_config(s, &pci_cmd, sizeof(pci_cmd),
                                   PCI_COMMAND) < 0) {
        goto out;
    }
out:
    return ret;
}

static void nvme_vfio_ram_block_added(RAMBlockNotifier *n, void *host, size_t size)
{
    NVMeVFIOState *s = container_of(n, NVMeVFIOState, ram_notifier);
    trace_nvme_vfio_ram_block_added(host, size);
    nvme_vfio_dma_map(s, host, size, false, NULL);
}

static void nvme_vfio_ram_block_removed(RAMBlockNotifier *n, void *host, size_t size)
{
    NVMeVFIOState *s = container_of(n, NVMeVFIOState, ram_notifier);
    if (host) {
        trace_nvme_vfio_ram_block_removed(host, size);
        nvme_vfio_dma_unmap(s, host);
    }
}

/**
 * Open a PCI device, e.g. "0000:00:01.0".
 */
NVMeVFIOState *nvme_vfio_open_pci(const char *device, Error **errp)
{
    int r;
    NVMeVFIOState *s = g_new0(NVMeVFIOState, 1);

    r = nvme_vfio_init_pci(s, device, errp);
    if (r) {
        g_free(s);
        return NULL;
    }

    /* TODO: handle existing ram blocks (or hot-plugging a nvme:// driver )*/
    s->ram_notifier.ram_block_added = nvme_vfio_ram_block_added;
    s->ram_notifier.ram_block_removed = nvme_vfio_ram_block_removed;
    ram_block_notifier_add(&s->ram_notifier);
    s->low_water_mark = NVME_VFIO_IOVA_MIN;
    s->high_water_mark = NVME_VFIO_IOVA_MAX;

    return s;
}

static void nvme_vfio_dump_mapping(IOVAMapping *m)
{
    printf("  vfio mapping %p %lx to %lx\n", m->host, m->size, m->iova);
}

static void nvme_vfio_dump_mappings(NVMeVFIOState *s)
{
    int i;

    if (NVME_DEBUG) {
        printf("vfio mappings\n");
        for (i = 0; i < s->nr_mappings; ++i) {
            nvme_vfio_dump_mapping(&s->mappings[i]);
        }
    }
}

/**
 * Find the mapping entry that contains [host, host + size) and set @index to
 * the position. If no entry contains it, @index is the position _after_ which
 * to insert the new mapping. IOW, it is the index of the largest element that
 * is smaller than @host, or -1 if no entry is. */
static IOVAMapping *nvme_vfio_find_mapping(NVMeVFIOState *s, void *host,
                                           int *index)
{
    IOVAMapping *p = s->mappings;
    IOVAMapping *q = p ? p + s->nr_mappings - 1 : NULL;
    IOVAMapping *mid = p ? p + (q - p) / 2 : NULL;
    trace_nvme_vfio_find_mapping(s, host);
    if (!p) {
        *index = -1;
        return NULL;
    }
    while (true) {
        mid = p + (q - p) / 2;
        if (mid == p) {
            break;
        }
        if (mid->host > host) {
            q = mid;
        } else if (mid->host < host) {
            p = mid;
        } else {
            break;
        }
    }
    if (mid->host > host) {
        mid--;
    } else if (mid < &s->mappings[s->nr_mappings - 1]
               && (mid + 1)->host <= host) {
        mid++;
    }
    *index = mid - &s->mappings[0];
    if (mid >= &s->mappings[0] &&
        mid->host <= host && mid->host + mid->size > host) {
        assert(mid < &s->mappings[s->nr_mappings]);
        return mid;
    }
    return NULL;
}

/**
 * Allocate IOVA and and create a new mapping record and insert it in @s. If
 * contiguous is true, the mapped IOVA must be contiguous, otherwise
 * segmentation is allowed to make the allocation more likely to succeed.
 */
static IOVAMapping *nvme_vfio_new_mapping(NVMeVFIOState *s,
                                          void *host, size_t size,
                                          int index, uint64_t iova)
{
    int shift;
    IOVAMapping m = {.host = host, .size = size, iova = iova};
    IOVAMapping *insert;

    assert(QEMU_IS_ALIGNED(size, getpagesize()));
    assert(QEMU_IS_ALIGNED(s->low_water_mark, getpagesize()));
    assert(QEMU_IS_ALIGNED(s->high_water_mark, getpagesize()));
    trace_nvme_vfio_new_mapping(s, host, size, index, iova);

    assert(index >= 0);
    s->nr_mappings++;
    s->mappings = g_realloc_n(s->mappings, sizeof(s->mappings[0]),
                              s->nr_mappings);
    insert = &s->mappings[index];
    shift = s->nr_mappings - index - 1;
    if (shift) {
        memmove(insert + 1, insert, shift * sizeof(s->mappings[0]));
    }
    *insert = m;
    return insert;
}

/* Remove the mapping record from @s and undo the IOVA mapping with VFIO. */
static void nvme_vfio_free_mapping(NVMeVFIOState *s, IOVAMapping *mapping,
                                   bool can_fail)
{
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = 0,
        .iova = mapping->iova,
        .size = mapping->size,
    };
    assert(mapping->size > 0);
    assert(QEMU_IS_ALIGNED(mapping->size, getpagesize()));
    if (ioctl(s->container, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        if (!can_fail) {
            error_report("VFIO_UNMAP_DMA: %d", -errno);
        }
    }
}

/* Do the DMA mapping with VFIO. */
static int nvme_vfio_do_mapping(NVMeVFIOState *s, void *host, size_t size,
                                uint64_t iova)
{
    struct vfio_iommu_type1_dma_map dma_map = {
        .argsz = sizeof(dma_map),
        .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
        .iova = iova,
        .vaddr = (uintptr_t)host,
        .size = size,
    };
    trace_nvme_vfio_do_mapping(s, host, size, iova);

    if (ioctl(s->container, VFIO_IOMMU_MAP_DMA, &dma_map)) {
        error_report("VFIO_MAP_DMA: %d", -errno);
        return -errno;
    }
    return 0;
}

/* Check if the mapping list is (ascending) ordered. */
static bool nvme_vfio_verify_mappings(NVMeVFIOState *s)
{
    int i;
    for (i = 0; i < s->nr_mappings - 1; ++i) {
        if (!(s->mappings[i].host < s->mappings[i + 1].host)) {
            fprintf(stderr, "item %d not sorted!\n", i);
            nvme_vfio_dump_mappings(s);
            return false;
        }
        if (!(s->mappings[i].host + s->mappings[i].size <=
              s->mappings[i + 1].host)) {
            fprintf(stderr, "item %d overlap with next!\n", i);
            nvme_vfio_dump_mappings(s);
            return false;
        }
    }
    return true;
}

/* Map [host, host + size) area into a contiguous IOVA address space, and store
 * the result in @iova if not NULL. The area must be aligned to page size, and
 * mustn't overlap or cross boundary of existing mapping areas.
 */
int nvme_vfio_dma_map(NVMeVFIOState *s, void *host, size_t size,
                      bool temporary, uint64_t *iova)
{
    int ret;
    int index;
    IOVAMapping *mapping;
    uint64_t iova0;

    assert(QEMU_PTR_IS_ALIGNED(host, getpagesize()));
    assert(QEMU_IS_ALIGNED(size, getpagesize()));
    trace_nvme_vfio_dma_map(s, host, size, temporary, iova);
    mapping = nvme_vfio_find_mapping(s, host, &index);
    if (mapping) {
        iova0 = mapping->iova + ((uint8_t *)host - (uint8_t *)mapping->host);
    } else {
        if (s->high_water_mark - s->low_water_mark + 1 < size) {
            return -ENOMEM;
        }
        if (!temporary) {
            iova0 = s->low_water_mark;
            mapping = nvme_vfio_new_mapping(s, host, size, index + 1, iova0);
            if (!mapping) {
                return -ENOMEM;
            }
            if (!(mapping->host <= host &&
                  mapping->host + mapping->size >= host + size)) {
                trace_nvme_vfio_dma_map_invalid(s, mapping->host, mapping->size,
                                                host, size);
                return -EINVAL;
            }
            assert(nvme_vfio_verify_mappings(s));
            ret = nvme_vfio_do_mapping(s, host, size, iova0);
            if (ret) {
                nvme_vfio_free_mapping(s, mapping, true);
                return ret;
            }
            s->low_water_mark += size;
            nvme_vfio_dump_mappings(s);
        } else {
            iova0 = s->high_water_mark - size;
            ret = nvme_vfio_do_mapping(s, host, size, iova0);
            if (ret) {
                return ret;
            }
            s->high_water_mark -= size;
        }
    }
    if (iova) {
        *iova = iova0;
    }
    return 0;
}

/* Reset the high watermark to free all previous "temporary" mappsing. */
int nvme_vfio_dma_reset_temporary(NVMeVFIOState *s)
{
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = 0,
        .iova = s->high_water_mark,
        .size = NVME_VFIO_IOVA_MAX - s->high_water_mark,
    };
    trace_nvme_vfio_dma_reset_temporary(s);
    if (ioctl(s->container, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        error_report("VFIO_UNMAP_DMA: %d", -errno);
        return -errno;
    }
    s->high_water_mark = NVME_VFIO_IOVA_MAX;
    return 0;
}

/* Unmapping the whole area that was previously mapped with
 * nvme_vfio_dma_map(). */
void nvme_vfio_dma_unmap(NVMeVFIOState *s, void *host)
{
    int index = 0;
    IOVAMapping *m;

    if (!host) {
        return;
    }

    trace_nvme_vfio_dma_unmap(s, host);
    m = nvme_vfio_find_mapping(s, host, &index);
    if (!m) {
        return;
    }
    nvme_vfio_free_mapping(s, m, false);
    assert(s->nr_mappings);
    memmove(&s->mappings[index], &s->mappings[index + 1],
            sizeof(s->mappings[0]) * (s->nr_mappings - index - 1));
    s->nr_mappings--;
    s->mappings = g_realloc_n(s->mappings, sizeof(s->mappings[0]),
                              s->nr_mappings);
}
void nvme_vfio_reset(NVMeVFIOState *s)
{
    ioctl(s->device, VFIO_DEVICE_RESET);
}

/* Close and free the VFIO resources. */
void nvme_vfio_close(NVMeVFIOState *s)
{
    int i;

    if (!s) {
        return;
    }
    for (i = 0; i < s->nr_mappings; ++i) {
        nvme_vfio_free_mapping(s, &s->mappings[i], false);
    }
    ram_block_notifier_remove(&s->ram_notifier);
    nvme_vfio_reset(s);
    close(s->device);
    close(s->group);
    close(s->container);
}
