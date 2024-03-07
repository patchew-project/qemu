/*
 * QEMU educational PCI device
 *
 * Copyright (c) 2012-2015 Jiri Slaby
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define TYPE_PCI_EDU_DEVICE "edu"
typedef struct EduState EduState;
DECLARE_INSTANCE_CHECKER(EduState, EDU,
                         TYPE_PCI_EDU_DEVICE)

#define FACT_IRQ        0x00000001
#define DMA_IRQ         0x00000100

#define DMA_START       0x40000
#define DMA_SIZE        4096

/*
 * Number of tries before giving up on page request group response.
 * Given the timer callback is scheduled to be run again after 100ms,
 * 10 tries give roughly a second for the PRGR notification to be
 * received.
 */
#define NUM_TRIES       10

struct EduState {
    PCIDevice pdev;
    MemoryRegion mmio;

    QemuThread thread;
    QemuMutex thr_mutex;
    QemuCond thr_cond;
    bool stopping;

    bool enable_pasid;
    uint32_t try;

    uint32_t addr4;
    uint32_t fact;
#define EDU_STATUS_COMPUTING    0x01
#define EDU_STATUS_IRQFACT      0x80
    uint32_t status;

    uint32_t irq_status;

#define EDU_DMA_RUN             0x1
#define EDU_DMA_DIR(cmd)        (((cmd) & 0x2) >> 1)
# define EDU_DMA_FROM_PCI       0
# define EDU_DMA_TO_PCI         1
#define EDU_DMA_IRQ             0x4
#define EDU_DMA_PV              0x8
#define EDU_DMA_PASID(cmd)      (((cmd) >> 8) & ((1U << 20) - 1))

    struct dma_state {
        dma_addr_t src;
        dma_addr_t dst;
        dma_addr_t cnt;
        dma_addr_t cmd;
    } dma;
    QEMUTimer dma_timer;
    char dma_buf[DMA_SIZE];
    uint64_t dma_mask;

    MemoryListener iommu_listener;
    QLIST_HEAD(, edu_iommu) iommu_list;

    bool prgr_rcvd;
    bool prgr_success;
};

struct edu_iommu {
    EduState *edu;
    IOMMUMemoryRegion *iommu_mr;
    hwaddr iommu_offset;
    IOMMUNotifier n;
    QLIST_ENTRY(edu_iommu) iommu_next;
};

static bool edu_msi_enabled(EduState *edu)
{
    return msi_enabled(&edu->pdev);
}

static void edu_raise_irq(EduState *edu, uint32_t val)
{
    edu->irq_status |= val;
    if (edu->irq_status) {
        if (edu_msi_enabled(edu)) {
            msi_notify(&edu->pdev, 0);
        } else {
            pci_set_irq(&edu->pdev, 1);
        }
    }
}

static void edu_lower_irq(EduState *edu, uint32_t val)
{
    edu->irq_status &= ~val;

    if (!edu->irq_status && !edu_msi_enabled(edu)) {
        pci_set_irq(&edu->pdev, 0);
    }
}

static bool within(uint64_t addr, uint64_t start, uint64_t end)
{
    return start <= addr && addr < end;
}

static void edu_check_range(uint64_t addr, uint64_t size1, uint64_t start,
                uint64_t size2)
{
    uint64_t end1 = addr + size1;
    uint64_t end2 = start + size2;

    if (within(addr, start, end2) &&
            end1 > addr && end1 <= end2) {
        return;
    }

    hw_error("EDU: DMA range 0x%016"PRIx64"-0x%016"PRIx64
             " out of bounds (0x%016"PRIx64"-0x%016"PRIx64")!",
            addr, end1 - 1, start, end2 - 1);
}

static dma_addr_t edu_clamp_addr(const EduState *edu, dma_addr_t addr)
{
    dma_addr_t res = addr;
    return res;
}

static bool __find_iommu_mr_cb(Int128 start, Int128 len, const MemoryRegion *mr,
    hwaddr offset_in_region, void *opaque)
{
    IOMMUMemoryRegion **iommu_mr = opaque;
    *iommu_mr = memory_region_get_iommu((MemoryRegion *)mr);
    return *iommu_mr != NULL;
}

static int pci_dma_perm(PCIDevice *pdev, dma_addr_t iova, MemTxAttrs attrs)
{
    IOMMUMemoryRegion *iommu_mr = NULL;
    IOMMUMemoryRegionClass *imrc;
    int iommu_idx;
    FlatView *fv;
    EduState *edu = EDU(pdev);
    struct edu_iommu *iommu;

    RCU_READ_LOCK_GUARD();

    fv = address_space_to_flatview(pci_get_address_space(pdev));

    /* Find first IOMMUMemoryRegion */
    flatview_for_each_range(fv, __find_iommu_mr_cb, &iommu_mr);

    if (iommu_mr) {
        imrc = memory_region_get_iommu_class_nocheck(iommu_mr);

        /* IOMMU Index is mapping to memory attributes (PASID, etc) */
        iommu_idx = imrc->attrs_to_index ?
                    imrc->attrs_to_index(iommu_mr, attrs) : 0;

        /* Update IOMMU notifiers with proper index */
        QLIST_FOREACH(iommu, &edu->iommu_list, iommu_next) {
            if (iommu->iommu_mr == iommu_mr &&
                iommu->n.iommu_idx != iommu_idx) {
                memory_region_unregister_iommu_notifier(
                    MEMORY_REGION(iommu->iommu_mr), &iommu->n);
                iommu->n.iommu_idx = iommu_idx;
                memory_region_register_iommu_notifier(
                    MEMORY_REGION(iommu->iommu_mr), &iommu->n, NULL);
            }
        }

        /* Translate request with IOMMU_NONE is an ATS request */
        IOMMUTLBEntry iotlb = imrc->translate(iommu_mr, iova, IOMMU_NONE,
                                              iommu_idx);

        return iotlb.perm;
    }

    return IOMMU_NONE;
}

static void edu_dma_timer(void *opaque)
{
    EduState *edu = opaque;
    bool raise_irq = false;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    MemTxResult res;

    if (!(edu->dma.cmd & EDU_DMA_RUN)) {
        return;
    }

    if (edu->enable_pasid && (edu->dma.cmd & EDU_DMA_PV)) {
        attrs.unspecified = 0;
        attrs.pasid = EDU_DMA_PASID(edu->dma.cmd);
        attrs.requester_id = pci_requester_id(&edu->pdev);
        attrs.secure = 0;
    }

    if (EDU_DMA_DIR(edu->dma.cmd) == EDU_DMA_FROM_PCI) {
        uint64_t dst = edu->dma.dst;
        uint64_t src = edu_clamp_addr(edu, edu->dma.src);
        edu_check_range(dst, edu->dma.cnt, DMA_START, DMA_SIZE);
        dst -= DMA_START;
        if (edu->try-- == NUM_TRIES) {
            edu->prgr_rcvd = false;
            if (!(pci_dma_perm(&edu->pdev, src, attrs) & IOMMU_RO)) {
                timer_mod(&edu->dma_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
                return;
            }
        } else if (edu->try) {
            if (!edu->prgr_rcvd) {
                timer_mod(&edu->dma_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
                return;
            }
            if (!edu->prgr_success) {
                /* PRGR failure, fail DMA. */
                edu->dma.cmd &= ~EDU_DMA_RUN;
                return;
            }
        } else {
            /* timeout, fail DMA. */
            edu->dma.cmd &= ~EDU_DMA_RUN;
            return;
        }
        res = pci_dma_rw(&edu->pdev, src, edu->dma_buf + dst, edu->dma.cnt,
            DMA_DIRECTION_TO_DEVICE, attrs);
        if (res != MEMTX_OK) {
            hw_error("EDU: DMA transfer TO 0x%"PRIx64" failed.\n", dst);
        }
    } else {
        uint64_t src = edu->dma.src;
        uint64_t dst = edu_clamp_addr(edu, edu->dma.dst);
        edu_check_range(src, edu->dma.cnt, DMA_START, DMA_SIZE);
        src -= DMA_START;
        if (edu->try-- == NUM_TRIES) {
            edu->prgr_rcvd = false;
            if (!(pci_dma_perm(&edu->pdev, dst, attrs) & IOMMU_WO)) {
                timer_mod(&edu->dma_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
                return;
            }
        } else if (edu->try) {
            if (!edu->prgr_rcvd) {
                timer_mod(&edu->dma_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
                return;
            }
            if (!edu->prgr_success) {
                /* PRGR failure, fail DMA. */
                edu->dma.cmd &= ~EDU_DMA_RUN;
                return;
            }
        } else {
            /* timeout, fail DMA. */
            edu->dma.cmd &= ~EDU_DMA_RUN;
            return;
        }
        res = pci_dma_rw(&edu->pdev, dst, edu->dma_buf + src, edu->dma.cnt,
            DMA_DIRECTION_FROM_DEVICE, attrs);
        if (res != MEMTX_OK) {
            hw_error("EDU: DMA transfer FROM 0x%"PRIx64" failed.\n", src);
        }
    }

    edu->dma.cmd &= ~EDU_DMA_RUN;
    if (edu->dma.cmd & EDU_DMA_IRQ) {
        raise_irq = true;
    }

    if (raise_irq) {
        edu_raise_irq(edu, DMA_IRQ);
    }
}

static void dma_rw(EduState *edu, bool write, dma_addr_t *val, dma_addr_t *dma,
                bool timer)
{
    if (write && (edu->dma.cmd & EDU_DMA_RUN)) {
        return;
    }

    if (write) {
        *dma = *val;
    } else {
        *val = *dma;
    }

    if (timer) {
        edu->try = NUM_TRIES;
        timer_mod(&edu->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
    }
}

static uint64_t edu_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    EduState *edu = opaque;
    uint64_t val = ~0ULL;

    if (addr < 0x80 && size != 4) {
        return val;
    }

    if (addr >= 0x80 && size != 4 && size != 8) {
        return val;
    }

    switch (addr) {
    case 0x00:
        val = 0x010000edu;
        break;
    case 0x04:
        val = edu->addr4;
        break;
    case 0x08:
        qemu_mutex_lock(&edu->thr_mutex);
        val = edu->fact;
        qemu_mutex_unlock(&edu->thr_mutex);
        break;
    case 0x20:
        val = qatomic_read(&edu->status);
        break;
    case 0x24:
        val = edu->irq_status;
        break;
    case 0x80:
        dma_rw(edu, false, &val, &edu->dma.src, false);
        break;
    case 0x88:
        dma_rw(edu, false, &val, &edu->dma.dst, false);
        break;
    case 0x90:
        dma_rw(edu, false, &val, &edu->dma.cnt, false);
        break;
    case 0x98:
        dma_rw(edu, false, &val, &edu->dma.cmd, false);
        break;
    }

    return val;
}

static void edu_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
    EduState *edu = opaque;

    if (addr < 0x80 && size != 4) {
        return;
    }

    if (addr >= 0x80 && size != 4 && size != 8) {
        return;
    }

    switch (addr) {
    case 0x04:
        edu->addr4 = ~val;
        break;
    case 0x08:
        if (qatomic_read(&edu->status) & EDU_STATUS_COMPUTING) {
            break;
        }
        /*
         * EDU_STATUS_COMPUTING cannot go 0->1 concurrently, because it is only
         * set in this function and it is under the iothread mutex.
         */
        qemu_mutex_lock(&edu->thr_mutex);
        edu->fact = val;
        qatomic_or(&edu->status, EDU_STATUS_COMPUTING);
        qemu_cond_signal(&edu->thr_cond);
        qemu_mutex_unlock(&edu->thr_mutex);
        break;
    case 0x20:
        if (val & EDU_STATUS_IRQFACT) {
            qatomic_or(&edu->status, EDU_STATUS_IRQFACT);
            /* Order check of the COMPUTING flag after setting IRQFACT.  */
            smp_mb__after_rmw();
        } else {
            qatomic_and(&edu->status, ~EDU_STATUS_IRQFACT);
        }
        break;
    case 0x60:
        edu_raise_irq(edu, val);
        break;
    case 0x64:
        edu_lower_irq(edu, val);
        break;
    case 0x80:
        dma_rw(edu, true, &val, &edu->dma.src, false);
        break;
    case 0x88:
        dma_rw(edu, true, &val, &edu->dma.dst, false);
        break;
    case 0x90:
        dma_rw(edu, true, &val, &edu->dma.cnt, false);
        break;
    case 0x98:
        if (!(val & EDU_DMA_RUN)) {
            break;
        }
        dma_rw(edu, true, &val, &edu->dma.cmd, true);
        break;
    }
}

static const MemoryRegionOps edu_mmio_ops = {
    .read = edu_mmio_read,
    .write = edu_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },

};

/*
 * We purposely use a thread, so that users are forced to wait for the status
 * register.
 */
static void *edu_fact_thread(void *opaque)
{
    EduState *edu = opaque;

    while (1) {
        uint32_t val, ret = 1;

        qemu_mutex_lock(&edu->thr_mutex);
        while ((qatomic_read(&edu->status) & EDU_STATUS_COMPUTING) == 0 &&
                        !edu->stopping) {
            qemu_cond_wait(&edu->thr_cond, &edu->thr_mutex);
        }

        if (edu->stopping) {
            qemu_mutex_unlock(&edu->thr_mutex);
            break;
        }

        val = edu->fact;
        qemu_mutex_unlock(&edu->thr_mutex);

        while (val > 0) {
            ret *= val--;
        }

        /*
         * We should sleep for a random period here, so that students are
         * forced to check the status properly.
         */

        qemu_mutex_lock(&edu->thr_mutex);
        edu->fact = ret;
        qemu_mutex_unlock(&edu->thr_mutex);
        qatomic_and(&edu->status, ~EDU_STATUS_COMPUTING);

        /* Clear COMPUTING flag before checking IRQFACT.  */
        smp_mb__after_rmw();

        if (qatomic_read(&edu->status) & EDU_STATUS_IRQFACT) {
            bql_lock();
            edu_raise_irq(edu, FACT_IRQ);
            bql_unlock();
        }
    }

    return NULL;
}

static void edu_iommu_ats_prgr_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    struct edu_iommu *iommu = container_of(n, struct edu_iommu, n);
    EduState *edu = iommu->edu;
    edu->prgr_success = (iotlb->perm != IOMMU_NONE);
    barrier();
    edu->prgr_rcvd = true;
}

static void edu_iommu_ats_inval_notify(IOMMUNotifier *n,
                                       IOMMUTLBEntry *iotlb)
{

}

static void edu_iommu_region_add(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    EduState *edu = container_of(listener, EduState, iommu_listener);
    struct edu_iommu *iommu;
    Int128 end;
    int iommu_idx;
    IOMMUMemoryRegion *iommu_mr;

    if (!memory_region_is_iommu(section->mr)) {
        return;
    }

    iommu_mr = IOMMU_MEMORY_REGION(section->mr);

    /* Register ATS.INVAL notifier */
    iommu = g_malloc0(sizeof(*iommu));
    iommu->iommu_mr = iommu_mr;
    iommu->iommu_offset = section->offset_within_address_space -
                          section->offset_within_region;
    iommu->edu = edu;
    end = int128_add(int128_make64(section->offset_within_region),
                     section->size);
    end = int128_sub(end, int128_one());
    iommu_idx = memory_region_iommu_attrs_to_index(iommu_mr,
                                                   MEMTXATTRS_UNSPECIFIED);
    iommu_notifier_init(&iommu->n, edu_iommu_ats_inval_notify,
                        IOMMU_NOTIFIER_DEVIOTLB_UNMAP,
                        section->offset_within_region,
                        int128_get64(end),
                        iommu_idx);
    memory_region_register_iommu_notifier(section->mr, &iommu->n, NULL);
    QLIST_INSERT_HEAD(&edu->iommu_list, iommu, iommu_next);

    /* Register ATS.PRGR notifier */
    iommu = g_memdup2(iommu, sizeof(*iommu));
    iommu_notifier_init(&iommu->n, edu_iommu_ats_prgr_notify,
                        IOMMU_NOTIFIER_MAP,
                        section->offset_within_region,
                        int128_get64(end),
                        iommu_idx);
    memory_region_register_iommu_notifier(section->mr, &iommu->n, NULL);
    QLIST_INSERT_HEAD(&edu->iommu_list, iommu, iommu_next);
}

static void edu_iommu_region_del(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    EduState *edu = container_of(listener, EduState, iommu_listener);
    struct edu_iommu *iommu;

    if (!memory_region_is_iommu(section->mr)) {
        return;
    }

    QLIST_FOREACH(iommu, &edu->iommu_list, iommu_next) {
        if (MEMORY_REGION(iommu->iommu_mr) == section->mr &&
            iommu->n.start == section->offset_within_region) {
            memory_region_unregister_iommu_notifier(section->mr,
                                                    &iommu->n);
            QLIST_REMOVE(iommu, iommu_next);
            g_free(iommu);
            break;
        }
    }
}

static void pci_edu_realize(PCIDevice *pdev, Error **errp)
{
    EduState *edu = EDU(pdev);
    AddressSpace *dma_as = NULL;
    uint8_t *pci_conf = pdev->config;
    int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);

    pcie_endpoint_cap_init(pdev, 0);

    /* PCIe extended capability for PASID */
    pos = PCI_CONFIG_SPACE_SIZE;
    if (edu->enable_pasid) {
        /* PCIe Spec 7.8.9 PASID Extended Capability Structure */
        pcie_add_capability(pdev, PCI_EXT_CAP_ID_PASID, 1, pos, 8);
        pci_set_long(pdev->config + pos + 4, 0x00001400);
        pci_set_long(pdev->wmask + pos + 4,  0xfff0ffff);
        pos += 8;

        /* ATS Capability */
        pcie_ats_init(pdev, pos, true);
        pos += PCI_EXT_CAP_ATS_SIZEOF;

        /* PRI Capability */
        pcie_add_capability(pdev, PCI_EXT_CAP_ID_PRI, 1, pos, 16);
        /* PRI STOPPED */
        pci_set_long(pdev->config + pos +  4, 0x01000000);
        /* PRI ENABLE bit writable */
        pci_set_long(pdev->wmask  + pos +  4, 0x00000001);
        /* PRI Capacity Supported */
        pci_set_long(pdev->config + pos +  8, 0x00000080);
        /* PRI Allocations Allowed, 32 */
        pci_set_long(pdev->config + pos + 12, 0x00000040);
        pci_set_long(pdev->wmask  + pos + 12, 0x0000007f);

        pos += 8;
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    timer_init_ms(&edu->dma_timer, QEMU_CLOCK_VIRTUAL, edu_dma_timer, edu);

    qemu_mutex_init(&edu->thr_mutex);
    qemu_cond_init(&edu->thr_cond);
    qemu_thread_create(&edu->thread, "edu", edu_fact_thread,
                       edu, QEMU_THREAD_JOINABLE);

    memory_region_init_io(&edu->mmio, OBJECT(edu), &edu_mmio_ops, edu,
                    "edu-mmio", 1 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &edu->mmio);

    /* Register IOMMU listener */
    edu->iommu_listener = (MemoryListener) {
        .name = "edu-iommu",
        .region_add = edu_iommu_region_add,
        .region_del = edu_iommu_region_del,
    };

    dma_as = pci_device_iommu_address_space(pdev);
    memory_listener_register(&edu->iommu_listener, dma_as);
}

static void pci_edu_uninit(PCIDevice *pdev)
{
    EduState *edu = EDU(pdev);

    memory_listener_unregister(&edu->iommu_listener);

    qemu_mutex_lock(&edu->thr_mutex);
    edu->stopping = true;
    qemu_mutex_unlock(&edu->thr_mutex);
    qemu_cond_signal(&edu->thr_cond);
    qemu_thread_join(&edu->thread);

    qemu_cond_destroy(&edu->thr_cond);
    qemu_mutex_destroy(&edu->thr_mutex);

    timer_del(&edu->dma_timer);
    msi_uninit(pdev);
}


static void edu_instance_init(Object *obj)
{
    EduState *edu = EDU(obj);

    edu->dma_mask = ~0ULL;
    object_property_add_uint64_ptr(obj, "dma_mask",
                                   &edu->dma_mask, OBJ_PROP_FLAG_READWRITE);
}

static Property edu_properties[] = {
    DEFINE_PROP_BOOL("pasid", EduState, enable_pasid, TRUE),
    DEFINE_PROP_END_OF_LIST(),
};

static void edu_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    device_class_set_props(dc, edu_properties);
    k->realize = pci_edu_realize;
    k->exit = pci_edu_uninit;
    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = 0x11e8;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_edu_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { },
    };
    static const TypeInfo edu_info = {
        .name          = TYPE_PCI_EDU_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(EduState),
        .instance_init = edu_instance_init,
        .class_init    = edu_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&edu_info);
}
type_init(pci_edu_register_types)
