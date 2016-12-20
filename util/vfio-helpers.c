/*
 * VFIO helper functions
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
#include "qemu/vfio-helpers.h"
#include "qemu/error-report.h"
#include "standard-headers/linux/pci_regs.h"
#include "qemu/event_notifier.h"
#include "qemu/hbitmap.h"

#define VFIO_DEBUG 0

#define DPRINTF(...) \
    if (VFIO_DEBUG) { \
        printf(__VA_ARGS__); \
    }

/* XXX: Once VFIO exposes the iova bit width in the IOMMU capability interface,
 * we can use a runtime limit. It's also possible to do platform specific
 * detection by reading sysfs entries. Until then, 39 is a safe bet. */
#define QEMU_VFIO_IOVA_MAX (1ULL << 39)

/* DMA address space is managed in chunks. */
#define QEMU_VFIO_CHUNK_SIZE (2ULL << 20)

#define QEMU_VFIO_ALLOC_BITMAP_SIZE (QEMU_VFIO_IOVA_MAX / QEMU_VFIO_CHUNK_SIZE)

typedef struct IOVARange {
    uint64_t iova;
    uint64_t nr_pages;
    QSIMPLEQ_ENTRY(IOVARange) next;
} IOVARange;

typedef struct {
    /* Page aligned. */
    void *host;
    size_t size;
    /* The IOVA range list to which the [host, host + size) area is mapped. */
    QSIMPLEQ_HEAD(, IOVARange) iova_list;
} IOVAMapping;

struct QEMUVFIOState {
    int container;
    int group;
    int device;
    RAMBlockNotifier ram_notifier;
    struct vfio_region_info config_region_info, bar_region_info[6];

    /* Allocation bitmap of IOVA address space, each bit represents
     * QEMU_VFIO_CHUNK_SIZE bytes. Set bits mean free, unset for used/reserved.
     **/
    HBitmap *free_chunks;
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

static int qemu_vfio_pci_init_bar(QEMUVFIOState *s, unsigned int index,
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
void *qemu_vfio_pci_map_bar(QEMUVFIOState *s, int index, Error **errp)
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
void qemu_vfio_pci_unmap_bar(QEMUVFIOState *s, int index, void *bar)
{
    if (bar) {
        munmap(bar, MIN(8192, s->bar_region_info[index].size));
    }
}

/**
 * Initialize device IRQ with @irq_type and and register an event notifier.
 */
int qemu_vfio_pci_init_irq(QEMUVFIOState *s, EventNotifier *e,
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

static int qemu_vfio_pci_read_config(QEMUVFIOState *s, void *buf, int size, int ofs)
{
    if (pread(s->device, buf, size, s->config_region_info.offset + ofs) == size) {
        return 0;
    }
    return -1;
}

static int qemu_vfio_pci_write_config(QEMUVFIOState *s, void *buf, int size, int ofs)
{
    if (pwrite(s->device, buf, size,
               s->config_region_info.offset + ofs) == size) {
        return 0;
    }

    return -1;
}

static int qemu_vfio_init_pci(QEMUVFIOState *s, const char *device,
                              Error **errp)
{
    int ret;
    int i;
    uint16_t pci_cmd;
    struct vfio_group_status group_status =
    { .argsz = sizeof(group_status) };
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
        ret = qemu_vfio_pci_init_bar(s, i, errp);
        if (ret) {
            goto out;
        }
    }

    /* Enable bus master */
    if (qemu_vfio_pci_read_config(s, &pci_cmd, sizeof(pci_cmd),
                                  PCI_COMMAND) < 0) {
        goto out;
    }
    pci_cmd |= PCI_COMMAND_MASTER;
    if (qemu_vfio_pci_write_config(s, &pci_cmd, sizeof(pci_cmd),
                                   PCI_COMMAND) < 0) {
        goto out;
    }
out:
    return ret;
}

static void qemu_vfio_ram_block_added(RAMBlockNotifier *n, void *host, size_t size)
{
    QEMUVFIOState *s = container_of(n, QEMUVFIOState, ram_notifier);
    /* XXX: handle existing ram regions? */
    DPRINTF("ram block added %p %lx\n", host, size);
    qemu_vfio_dma_map(s, host, size, true, NULL);
}

static void qemu_vfio_ram_block_removed(RAMBlockNotifier *n, void *host, size_t size)
{
    QEMUVFIOState *s = container_of(n, QEMUVFIOState, ram_notifier);
    if (host) {
        DPRINTF("ram block removed %p %lx\n", host, size);
        qemu_vfio_dma_unmap(s, host);
    }
}

/**
 * Open a PCI device, e.g. "0000:00:01.0".
 */
QEMUVFIOState *qemu_vfio_open_pci(const char *device, Error **errp)
{
    int r;
    QEMUVFIOState *s = g_new0(QEMUVFIOState, 1);

    r = qemu_vfio_init_pci(s, device, errp);
    if (r) {
        g_free(s);
        return NULL;
    }

    s->ram_notifier.ram_block_added = qemu_vfio_ram_block_added;
    s->ram_notifier.ram_block_removed = qemu_vfio_ram_block_removed;
    ram_block_notifier_add(&s->ram_notifier);
    s->free_chunks = hbitmap_alloc(QEMU_VFIO_ALLOC_BITMAP_SIZE, 0);
    hbitmap_set(s->free_chunks, 1, QEMU_VFIO_ALLOC_BITMAP_SIZE - 1);

    return s;
}

static void qemu_vfio_dump_mapping(IOVAMapping *m)
{
    DPRINTF("  vfio mapping %p %lx\n", m->host, m->size);
    IOVARange *r;

    QSIMPLEQ_FOREACH(r, &m->iova_list, next) {
        DPRINTF("   IOVA %lx len %lx\n", r->iova, r->nr_pages * getpagesize());
    }
}

static void qemu_vfio_dump_mappings(QEMUVFIOState *s)
{
    int i;
    DPRINTF("vfio mappings\n");
    for (i = 0; i < s->nr_mappings; ++i) {
        qemu_vfio_dump_mapping(&s->mappings[i]);
    }
}

/**
 * Find the mapping entry that contains [host, host + size) and set @index to
 * the position. If no entry contains it, @index is the position _after_ which
 * to insert the new mapping. IOW, it is the index of the largest element that
 * is smaller than @host, or -1 if no entry is. */
static IOVAMapping *qemu_vfio_find_mapping(QEMUVFIOState *s,
                                           void *host, size_t size,
                                           int *index)
{
    IOVAMapping *p = s->mappings;
    IOVAMapping *q = p ? p + s->nr_mappings - 1 : NULL;
    IOVAMapping *mid = p ? p + (q - p) / 2 : NULL;
    DPRINTF("qemu vfio find mapping %p %lx... ", host, size);
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
        assert(mid->host + mid->size >= host + size);
        DPRINTF("found, index %d\n", *index);
        return mid;
    }
    DPRINTF("not found, index %d\n", *index);
    return NULL;
}

/**
 * Allocate IOVA and and create a new mapping record and insert it in @s. If
 * contiguous is true, the mapped IOVA must be contiguous, otherwise
 * segmentation is allowed to make the allocation more likely to succeed.
 */
static IOVAMapping *qemu_vfio_new_mapping(QEMUVFIOState *s,
                                          void *host, size_t size,
                                          int index,
                                          bool contiguous)
{
    int i;
    int shift;
    const size_t pages_per_chunk = QEMU_VFIO_CHUNK_SIZE / getpagesize();
    size_t pages = DIV_ROUND_UP(size, getpagesize());
    size_t chunks = DIV_ROUND_UP(pages, pages_per_chunk);
    int64_t next;
    IOVAMapping m = {.host = host, .size = size};
    IOVAMapping *insert;
    HBitmapIter iter;
    IOVARange *r;

    if (DIV_ROUND_UP(pages, pages_per_chunk) > hbitmap_count(s->free_chunks)) {
        return NULL;
    }
    QSIMPLEQ_INIT(&m.iova_list);

    r = NULL;
    hbitmap_iter_init(&iter, s->free_chunks, 1);
    if (contiguous) {
        while (true) {
            bool satisfy = true;
            next = hbitmap_iter_next(&iter);
            if (next < 0) {
                return NULL;
            }
            for (i = 1; i < chunks; i++) {
                if (!hbitmap_get(s->free_chunks, next + i)) {
                    satisfy = false;
                    break;
                }
            }
            if (satisfy) {
                break;
            }
        }
        hbitmap_reset(s->free_chunks, next, chunks);
        r = g_new(IOVARange, 1);
        r->iova = next * pages_per_chunk * getpagesize();
        r->nr_pages = pages;
        QSIMPLEQ_INSERT_TAIL(&m.iova_list, r, next);
    } else {
        next = hbitmap_iter_next(&iter);
        while (pages) {
            uint64_t chunk;
            if (next < 0) {
                hbitmap_iter_init(&iter, s->free_chunks, 1);
                next = hbitmap_iter_next(&iter);
            }
            assert(next >= 0);
            chunk = next;
            DPRINTF("using chunk %ld\n", chunk);
            next = hbitmap_iter_next(&iter);
            hbitmap_reset(s->free_chunks, chunk, 1);
            if (r && r->iova + r->nr_pages == chunk * pages_per_chunk) {
                r->nr_pages += MIN(pages, pages_per_chunk);
            } else {
                r = g_new(IOVARange, 1);
                r->iova = chunk * pages_per_chunk * getpagesize();
                r->nr_pages = MIN(pages, pages_per_chunk);
                QSIMPLEQ_INSERT_TAIL(&m.iova_list, r, next);
            }
            pages -= MIN(pages, pages_per_chunk);
        }
    }

    assert(index >= 0);
    s->nr_mappings++;
    s->mappings = g_realloc_n(s->mappings, sizeof(s->mappings[0]),
                              s->nr_mappings);
    insert = &s->mappings[index];
    shift = s->nr_mappings - index - 1;
    DPRINTF("inserting to %d shift %d\n", index, shift);
    if (shift) {
        memmove(insert + 1, insert, shift * sizeof(s->mappings[0]));
    }
    *insert = m;
    return insert;
}

/* Remove the mapping record from @s and undo the IOVA mapping with VFIO. */
static void qemu_vfio_free_mapping(QEMUVFIOState *s, IOVAMapping *mapping,
                                   bool can_fail)
{
    IOVARange *r, *tmp;

    QSIMPLEQ_FOREACH_SAFE(r, &mapping->iova_list, next, tmp) {
        size_t size = r->nr_pages * getpagesize();
        struct vfio_iommu_type1_dma_unmap unmap = {
            .argsz = sizeof(unmap),
            .flags = 0,
            .iova = r->iova,
            .size = size,
        };
        if (ioctl(s->container, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
            if (!can_fail) {
                error_report("VFIO_UNMAP_DMA: %d", -errno);
            }
        }
        hbitmap_set(s->free_chunks, r->iova / QEMU_VFIO_CHUNK_SIZE,
                    DIV_ROUND_UP(size, QEMU_VFIO_CHUNK_SIZE));
        g_free(r);
    }
}

/* Do the DMA mapping with VFIO. */
static int qemu_vfio_do_mapping(QEMUVFIOState *s, IOVAMapping *mapping)
{
    IOVARange *r;
    uint8_t *p = mapping->host;

    QSIMPLEQ_FOREACH(r, &mapping->iova_list, next) {
        struct vfio_iommu_type1_dma_map dma_map = {
            .argsz = sizeof(dma_map),
            .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
            .iova = r->iova,
            .vaddr = (uintptr_t)p,
            .size = r->nr_pages * getpagesize(),
        };
        DPRINTF("vfio map %p pages %ld to %lx\n", p, r->nr_pages, r->iova);

        if (ioctl(s->container, VFIO_IOMMU_MAP_DMA, &dma_map)) {
            error_report("VFIO_MAP_DMA: %d", -errno);
            return -errno;
        }
        p += r->nr_pages * getpagesize();
    }
    return 0;
}

/* Check if the mapping list is (ascending) ordered. */
static bool qemu_vfio_verify_mappings(QEMUVFIOState *s)
{
    int i;
    for (i = 0; i < s->nr_mappings - 1; ++i) {
        if (!(s->mappings[i].host < s->mappings[i + 1].host)) {
            fprintf(stderr, "item %d not sorted!\n", i);
            qemu_vfio_dump_mappings(s);
            return false;
        }
        if (!(s->mappings[i].host + s->mappings[i].size <=
              s->mappings[i + 1].host)) {
            fprintf(stderr, "item %d overlap with next!\n", i);
            qemu_vfio_dump_mappings(s);
            return false;
        }
    }
    return true;
}

/* Map [host, host + size) area, and optionally store the IOVA in iova_list
 * buffer. The area must be aligned to page size, and mustn't overlap or cross
 * boundary of existing mappings.
 *
 * If the area is already mapped, this function can be used to retrieve the
 * IOVA by passing non-NULL iova_list: for contiguous=true, only one entry is
 * stored to @iova_list; otherwise, up to (size / page_size) entries will be
 * written. The caller must ensure the passed buffer is big enough.
 *
 * If the area is not mapped, this will try to allocate a sequence of free
 * IOVAs (contiguous can be enforced by passing true), and report them to
 * iova_list.
 */
int qemu_vfio_dma_map(QEMUVFIOState *s, void *host, size_t size,
                      bool contiguous, uint64_t *iova_list)
{
    int ret;
    int index;
    IOVAMapping *mapping;

    DPRINTF("vfio dma map %p %lx contiguous %d\n", host, size, contiguous);
    assert(QEMU_PTR_IS_ALIGNED(host, getpagesize()));
    assert(QEMU_IS_ALIGNED(size, getpagesize()));
    mapping = qemu_vfio_find_mapping(s, host, size, &index);
    if (!mapping) {
        mapping = qemu_vfio_new_mapping(s, host, size, index + 1, contiguous);
        if (!mapping) {
            return -ENOMEM;
        }
        assert(qemu_vfio_verify_mappings(s));
        ret = qemu_vfio_do_mapping(s, mapping);
        if (ret) {
            qemu_vfio_free_mapping(s, mapping, true);
            return ret;
        }
        qemu_vfio_dump_mappings(s);
    } else {
        /* XXX: check contiguous for existing mapping? */
    }
    if (!mapping) {
        return -ENOMEM;
    }
    if (iova_list) {
        uint64_t *p = iova_list;
        uint64_t skip_pages = (host - mapping->host) / getpagesize();
        IOVARange *r;
        for (r = QSIMPLEQ_FIRST(&mapping->iova_list);
             skip_pages > r->nr_pages;
             r = QSIMPLEQ_NEXT(r, next)) {
            skip_pages -= r->nr_pages;
        }
        while (size) {
            uint64_t i;
            assert(r);
            for (i = skip_pages; size && i < r->nr_pages; ++i) {
                *p = r->iova + i * getpagesize();
                assert(*p >= r->iova);
                assert(*p < r->iova + r->nr_pages * getpagesize());
                p++;
                size -= getpagesize();
                if (contiguous) {
                    goto out;
                }
            }
            r = QSIMPLEQ_NEXT(r, next);
            skip_pages = 0;
        }
    }
out:
    return 0;
}

/* Unmapping the whole area that was previously mapped with
 * qemu_vfio_dma_map(). */
void qemu_vfio_dma_unmap(QEMUVFIOState *s, void *host)
{
    int index = 0;
    IOVAMapping *m;

    if (!host) {
        return;
    }

    DPRINTF("vfio unmap %p\n", host);
    m = qemu_vfio_find_mapping(s, host, 4096, &index);
    if (!m) {
        return;
    }
    qemu_vfio_free_mapping(s, m, false);
    assert(s->nr_mappings);
    memmove(&s->mappings[index], &s->mappings[index + 1],
            sizeof(s->mappings[0]) * (s->nr_mappings - index - 1));
    s->nr_mappings--;
    s->mappings = g_realloc_n(s->mappings, sizeof(s->mappings[0]),
                              s->nr_mappings);
}

/* Close and free the VFIO resources. */
void qemu_vfio_close(QEMUVFIOState *s)
{
    int i;

    if (!s) {
        return;
    }
    for (i = 0; i < s->nr_mappings; ++i) {
        qemu_vfio_free_mapping(s, &s->mappings[i], false);
    }
    hbitmap_free(s->free_chunks);
    ram_block_notifier_remove(&s->ram_notifier);
    close(s->device);
    close(s->group);
    close(s->container);
}
