/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Apple DMA mapping PCI device
 *
 * A simple PCI device that receives batched DMA map/unmap requests from
 * the guest via a shared command page + doorbell register, resolves guest
 * physical addresses to host virtual addresses, and registers them with
 * the macOS DriverKit dext for DART mapping.
 *
 * Protocol:
 *   1. Guest allocates a command page and request/response buffers in RAM.
 *   2. Guest writes the command page GPA to BAR registers (one-time setup).
 *   3. Per batch: guest fills the command page and request buffer (no VMEXIT),
 *      then writes the doorbell register (single VMEXIT triggers processing).
 *   4. Device reads the command page, processes all entries, writes responses
 *      and status back to guest RAM before the doorbell write returns.
 *
 * Copyright (c) 2026 Scott J. Goldman
 */

#include "qemu/osdep.h"

#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "hw/vfio/apple-dext-client.h"
#include "hw/vfio/apple.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "system/memory.h"

#include "hw/pci/pci.h"

/* BAR0 register offsets */
#define APPLE_DMA_REG_VERSION       0x00    /* R:  protocol version */
#define APPLE_DMA_REG_MANAGED_BDF   0x04    /* R:  guest BDF this maps for */
#define APPLE_DMA_REG_MAX_ENTRIES   0x08    /* R:  max entries per batch */
#define APPLE_DMA_REG_STATUS        0x0C    /* R:  result of last doorbell */
#define APPLE_DMA_REG_CMD_GPA_LO    0x10    /* W:  command page GPA [31:0] */
#define APPLE_DMA_REG_CMD_GPA_HI    0x14    /* W:  command page GPA [63:32] */
#define APPLE_DMA_REG_DOORBELL      0x18    /* W:  any write triggers batch */
#define APPLE_DMA_BAR_SIZE          0x1000  /* page-aligned */

#define APPLE_DMA_VERSION           2
#define APPLE_DMA_MAX_ENTRIES       4096

/* Command types (in command page) */
#define APPLE_DMA_CMD_MAP           1
#define APPLE_DMA_CMD_UNMAP         2

/* Status codes */
#define APPLE_DMA_S_OK              0
#define APPLE_DMA_S_IOERR           1
#define APPLE_DMA_S_INVAL           3

/*
 * Command page layout (in guest RAM, 32 bytes):
 *
 *   0x00  uint32_t  type         MAP=1, UNMAP=2
 *   0x04  uint32_t  count        number of entries
 *   0x08  uint32_t  status       (written by device) 0=OK
 *   0x0C  uint32_t  reserved
 *   0x10  uint64_t  req_gpa      GPA of request entries array
 *   0x18  uint64_t  resp_gpa     GPA of response entries array
 */
#define CMD_OFF_TYPE      0x00
#define CMD_OFF_COUNT     0x04
#define CMD_OFF_STATUS    0x08
#define CMD_OFF_REQ_GPA   0x10
#define CMD_OFF_RESP_GPA  0x18
#define CMD_PAGE_SIZE     0x20

/*
 * Map request entry (16 bytes):
 *   uint64_t gpa, uint32_t len, uint32_t flags
 *
 * Map response entry (24 bytes):
 *   uint64_t id, uint64_t dma_addr, uint32_t dma_len, uint32_t status
 *
 * Unmap request entry (16 bytes):
 *   uint64_t id, uint64_t size
 *
 * Unmap response entry (16 bytes):
 *   uint64_t id, uint32_t status, uint32_t reserved
 */

typedef struct AppleDMAMapReq {
    uint64_t gpa;
    uint32_t len;
    uint32_t flags;
} QEMU_PACKED AppleDMAMapReq;

typedef struct AppleDMAMapResp {
    uint64_t id;
    uint64_t dma_addr;
    uint32_t dma_len;
    uint32_t status;
} QEMU_PACKED AppleDMAMapResp;

typedef struct AppleDMAUnmapReq {
    uint64_t id;
    uint64_t size;
} QEMU_PACKED AppleDMAUnmapReq;

typedef struct AppleDMAUnmapResp {
    uint64_t id;
    uint32_t status;
    uint32_t reserved;
} QEMU_PACKED AppleDMAUnmapResp;

#define TYPE_APPLE_DMA_PCI "apple-dma-pci"
OBJECT_DECLARE_SIMPLE_TYPE(AppleDMAState, APPLE_DMA_PCI)

struct AppleDMAState {
    PCIDevice parent_obj;

    MemoryRegion bar;

    /* Configuration (set via properties) */
    uint32_t managed_bdf;
    uint32_t max_entries;
    uint32_t apple_host_bus;
    uint32_t apple_host_device;
    uint32_t apple_host_function;

    /* Runtime state */
    uint64_t cmd_gpa;
    uint32_t last_status;
    io_connect_t dext_conn;
    bool shared_dext_conn;
};

/* ------------------------------------------------------------------ */
/* DMA backend operations                                              */
/* ------------------------------------------------------------------ */

static bool apple_dma_backend_map(AppleDMAState *s, uint64_t gpa, uint32_t size,
                                  uint64_t *out_dma_addr, uint32_t *out_dma_len)
{
    hwaddr map_len = size;
    void *hva;
    uint64_t bus_addr = 0, bus_len = 0;

    hva = dma_memory_map(&address_space_memory, gpa, &map_len,
                         DMA_DIRECTION_TO_DEVICE, MEMTXATTRS_UNSPECIFIED);
    if (!hva || map_len < size) {
        if (hva) {
            dma_memory_unmap(&address_space_memory, hva, map_len,
                             DMA_DIRECTION_TO_DEVICE, 0);
        }
        return false;
    }

    /*
     * Use the GPA as the dext lookup key. The dext treats this as an
     * opaque handle for matching register/unregister calls; the actual
     * DMA bus address is assigned by the platform and returned in
     * bus_addr.
     */
    if (s->dext_conn != IO_OBJECT_NULL) {
        kern_return_t kr;

        kr = apple_dext_register_dma(s->dext_conn, gpa,
                                     (uint64_t)hva, size,
                                     &bus_addr, &bus_len);
        dma_memory_unmap(&address_space_memory, hva, map_len,
                         DMA_DIRECTION_TO_DEVICE, 0);
        if (kr != KERN_SUCCESS) {
            return false;
        }
        *out_dma_addr = bus_addr;
        *out_dma_len = (uint32_t)bus_len;
    } else {
        dma_memory_unmap(&address_space_memory, hva, map_len,
                         DMA_DIRECTION_TO_DEVICE, 0);
        *out_dma_addr = gpa;
        *out_dma_len = size;
    }

    return true;
}

static uint32_t apple_dma_backend_unmap(AppleDMAState *s, uint64_t id)
{
    if (s->dext_conn != IO_OBJECT_NULL) {
        kern_return_t kr;

        kr = apple_dext_unregister_dma(s->dext_conn, id);
        if (kr != KERN_SUCCESS) {
            return APPLE_DMA_S_IOERR;
        }
    }

    return APPLE_DMA_S_OK;
}

/* ------------------------------------------------------------------ */
/* Doorbell — process a batch from the command page                    */
/* ------------------------------------------------------------------ */

static void apple_dma_handle_map(AppleDMAState *s, uint64_t req_gpa,
                                 uint64_t resp_gpa, uint32_t count)
{
    AddressSpace *as = &address_space_memory;
    hwaddr req_len = count * sizeof(AppleDMAMapReq);
    hwaddr resp_len = count * sizeof(AppleDMAMapResp);
    AppleDMAMapReq *reqs;
    AppleDMAMapResp *resps;
    uint32_t i;
    bool ok = true;

    reqs = dma_memory_map(as, req_gpa, &req_len, DMA_DIRECTION_TO_DEVICE,
                          MEMTXATTRS_UNSPECIFIED);
    if (!reqs || req_len < count * sizeof(AppleDMAMapReq)) {
        if (reqs) {
            dma_memory_unmap(as, reqs, req_len, DMA_DIRECTION_TO_DEVICE, 0);
        }
        s->last_status = APPLE_DMA_S_INVAL;
        return;
    }

    resps = dma_memory_map(as, resp_gpa, &resp_len, DMA_DIRECTION_FROM_DEVICE,
                           MEMTXATTRS_UNSPECIFIED);
    if (!resps || resp_len < count * sizeof(AppleDMAMapResp)) {
        if (resps) {
            dma_memory_unmap(as, resps, resp_len,
                             DMA_DIRECTION_FROM_DEVICE, 0);
        }
        dma_memory_unmap(as, reqs, req_len, DMA_DIRECTION_TO_DEVICE, 0);
        s->last_status = APPLE_DMA_S_INVAL;
        return;
    }

    for (i = 0; i < count; i++) {
        uint64_t gpa = le64_to_cpu(reqs[i].gpa);
        uint64_t dma_addr = 0;
        uint32_t dma_len = 0;

        if (apple_dma_backend_map(s, gpa, le32_to_cpu(reqs[i].len),
                                  &dma_addr, &dma_len)) {
            resps[i].id = cpu_to_le64(gpa);
            resps[i].dma_addr = cpu_to_le64(dma_addr);
            resps[i].dma_len = cpu_to_le32(dma_len);
            resps[i].status = cpu_to_le32(APPLE_DMA_S_OK);
        } else {
            resps[i].status = cpu_to_le32(APPLE_DMA_S_IOERR);
            ok = false;
        }
    }

    s->last_status = ok ? APPLE_DMA_S_OK : APPLE_DMA_S_IOERR;
    dma_memory_unmap(as, resps, resp_len, DMA_DIRECTION_FROM_DEVICE,
                     resp_len);
    dma_memory_unmap(as, reqs, req_len, DMA_DIRECTION_TO_DEVICE, 0);
}

static void apple_dma_handle_unmap(AppleDMAState *s, uint64_t req_gpa,
                                   uint64_t resp_gpa, uint32_t count)
{
    AddressSpace *as = &address_space_memory;
    hwaddr req_len = count * sizeof(AppleDMAUnmapReq);
    hwaddr resp_len = count * sizeof(AppleDMAUnmapResp);
    AppleDMAUnmapReq *reqs;
    AppleDMAUnmapResp *resps;
    uint32_t i;
    bool ok = true;

    reqs = dma_memory_map(as, req_gpa, &req_len, DMA_DIRECTION_TO_DEVICE,
                          MEMTXATTRS_UNSPECIFIED);
    if (!reqs || req_len < count * sizeof(AppleDMAUnmapReq)) {
        if (reqs) {
            dma_memory_unmap(as, reqs, req_len, DMA_DIRECTION_TO_DEVICE, 0);
        }
        s->last_status = APPLE_DMA_S_INVAL;
        return;
    }

    resps = dma_memory_map(as, resp_gpa, &resp_len, DMA_DIRECTION_FROM_DEVICE,
                           MEMTXATTRS_UNSPECIFIED);
    if (!resps || resp_len < count * sizeof(AppleDMAUnmapResp)) {
        if (resps) {
            dma_memory_unmap(as, resps, resp_len,
                             DMA_DIRECTION_FROM_DEVICE, 0);
        }
        dma_memory_unmap(as, reqs, req_len, DMA_DIRECTION_TO_DEVICE, 0);
        s->last_status = APPLE_DMA_S_INVAL;
        return;
    }

    for (i = 0; i < count; i++) {
        uint64_t id = le64_to_cpu(reqs[i].id);
        uint32_t status = apple_dma_backend_unmap(s, id);

        resps[i].id = cpu_to_le64(id);
        resps[i].status = cpu_to_le32(status);
        if (status != APPLE_DMA_S_OK) {
            ok = false;
        }
    }

    s->last_status = ok ? APPLE_DMA_S_OK : APPLE_DMA_S_IOERR;
    dma_memory_unmap(as, resps, resp_len, DMA_DIRECTION_FROM_DEVICE,
                     resp_len);
    dma_memory_unmap(as, reqs, req_len, DMA_DIRECTION_TO_DEVICE, 0);
}

static void apple_dma_doorbell(AppleDMAState *s)
{
    AddressSpace *as = &address_space_memory;
    uint8_t cmd_buf[CMD_PAGE_SIZE];
    uint32_t type, count;
    uint64_t req_gpa, resp_gpa;
    uint32_t le_status;

    if (!s->cmd_gpa) {
        s->last_status = APPLE_DMA_S_INVAL;
        return;
    }

    if (dma_memory_read(as, s->cmd_gpa, cmd_buf, CMD_PAGE_SIZE,
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        s->last_status = APPLE_DMA_S_INVAL;
        return;
    }

    type = ldl_le_p(cmd_buf + CMD_OFF_TYPE);
    count = ldl_le_p(cmd_buf + CMD_OFF_COUNT);
    req_gpa = ldq_le_p(cmd_buf + CMD_OFF_REQ_GPA);
    resp_gpa = ldq_le_p(cmd_buf + CMD_OFF_RESP_GPA);

    if (!count || count > s->max_entries || !req_gpa || !resp_gpa) {
        s->last_status = APPLE_DMA_S_INVAL;
        goto write_status;
    }

    switch (type) {
    case APPLE_DMA_CMD_MAP:
        apple_dma_handle_map(s, req_gpa, resp_gpa, count);
        break;
    case APPLE_DMA_CMD_UNMAP:
        apple_dma_handle_unmap(s, req_gpa, resp_gpa, count);
        break;
    default:
        s->last_status = APPLE_DMA_S_INVAL;
        break;
    }

write_status:
    le_status = cpu_to_le32(s->last_status);
    dma_memory_write(as, s->cmd_gpa + CMD_OFF_STATUS, &le_status, 4,
                     MEMTXATTRS_UNSPECIFIED);
}

/* ------------------------------------------------------------------ */
/* MMIO BAR handlers                                                   */
/* ------------------------------------------------------------------ */

static uint64_t apple_dma_bar_read(void *opaque, hwaddr addr, unsigned size)
{
    AppleDMAState *s = opaque;

    switch (addr) {
    case APPLE_DMA_REG_VERSION:
        return APPLE_DMA_VERSION;
    case APPLE_DMA_REG_MANAGED_BDF:
        return s->managed_bdf;
    case APPLE_DMA_REG_MAX_ENTRIES:
        return s->max_entries;
    case APPLE_DMA_REG_STATUS:
        return s->last_status;
    default:
        return 0;
    }
}

static void apple_dma_bar_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    AppleDMAState *s = opaque;

    switch (addr) {
    case APPLE_DMA_REG_CMD_GPA_LO:
        s->cmd_gpa = deposit64(s->cmd_gpa, 0, 32, val);
        break;
    case APPLE_DMA_REG_CMD_GPA_HI:
        s->cmd_gpa = deposit64(s->cmd_gpa, 32, 32, val);
        break;
    case APPLE_DMA_REG_DOORBELL:
        apple_dma_doorbell(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps apple_dma_bar_ops = {
    .read = apple_dma_bar_read,
    .write = apple_dma_bar_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* ------------------------------------------------------------------ */
/* Dext connection                                                     */
/* ------------------------------------------------------------------ */

static bool apple_dma_connect_dext(AppleDMAState *s, Error **errp)
{
    io_connect_t conn;
    kern_return_t kr;

    conn = apple_vfio_dext_lookup(s->apple_host_bus, s->apple_host_device,
                                  s->apple_host_function);
    if (conn != IO_OBJECT_NULL) {
        s->dext_conn = conn;
        s->shared_dext_conn = true;
        return true;
    }

    conn = apple_dext_connect(s->apple_host_bus, s->apple_host_device,
                                  s->apple_host_function);
    if (conn == IO_OBJECT_NULL) {
        error_setg(errp,
                   "apple-dma: could not connect to dext for host PCI "
                   "%02x:%02x.%x",
                   s->apple_host_bus, s->apple_host_device,
                   s->apple_host_function);
        return false;
    }

    kr = apple_dext_claim(conn);
    if (kr != KERN_SUCCESS) {
        error_setg(errp,
                   "apple-dma: failed to claim dext-backed PCI device "
                   "%02x:%02x.%x (kr=0x%x)",
                   s->apple_host_bus, s->apple_host_device,
                   s->apple_host_function, kr);
        apple_dext_disconnect(conn);
        return false;
    }

    s->dext_conn = conn;
    s->shared_dext_conn = false;
    return true;
}

/* ------------------------------------------------------------------ */
/* PCI device lifecycle                                                */
/* ------------------------------------------------------------------ */

static void apple_dma_pci_realize(PCIDevice *pdev, Error **errp)
{
    AppleDMAState *s = APPLE_DMA_PCI(pdev);

    if (s->apple_host_bus == UINT32_MAX ||
        s->apple_host_device == UINT32_MAX ||
        s->apple_host_function == UINT32_MAX) {
        error_setg(errp, "apple-dma: requires x-apple-host-bus, "
                   "x-apple-host-device, and x-apple-host-function");
        return;
    }

    if (!s->max_entries) {
        s->max_entries = APPLE_DMA_MAX_ENTRIES;
    }

    if (!apple_dma_connect_dext(s, errp)) {
        return;
    }

    memory_region_init_io(&s->bar, OBJECT(s), &apple_dma_bar_ops, s,
                          "apple-dma-bar", APPLE_DMA_BAR_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar);
}

static void apple_dma_pci_exit(PCIDevice *pdev)
{
    AppleDMAState *s = APPLE_DMA_PCI(pdev);

    if (s->dext_conn != IO_OBJECT_NULL) {
        if (s->shared_dext_conn) {
            apple_vfio_dext_release(s->apple_host_bus, s->apple_host_device,
                                    s->apple_host_function, s->dext_conn);
        } else {
            apple_dext_disconnect(s->dext_conn);
        }
        s->dext_conn = IO_OBJECT_NULL;
    }
}

static const Property apple_dma_pci_properties[] = {
    DEFINE_PROP_UINT32("managed-bdf", AppleDMAState, managed_bdf, 0),
    DEFINE_PROP_UINT32("max-entries", AppleDMAState, max_entries, 0),
    DEFINE_PROP_UINT32("x-apple-host-bus", AppleDMAState,
                       apple_host_bus, UINT32_MAX),
    DEFINE_PROP_UINT32("x-apple-host-device", AppleDMAState,
                       apple_host_device, UINT32_MAX),
    DEFINE_PROP_UINT32("x-apple-host-function", AppleDMAState,
                       apple_host_function, UINT32_MAX),
};

static void apple_dma_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(klass);

    pdc->realize = apple_dma_pci_realize;
    pdc->exit = apple_dma_pci_exit;
    pdc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pdc->device_id = PCI_DEVICE_ID_REDHAT_APPLE_DMA;
    pdc->class_id = PCI_CLASS_SYSTEM_OTHER;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, apple_dma_pci_properties);
    dc->desc = "Apple DMA mapping device";
}

static const TypeInfo apple_dma_pci_info = {
    .name = TYPE_APPLE_DMA_PCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(AppleDMAState),
    .class_init = apple_dma_pci_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void apple_dma_pci_register(void)
{
    type_register_static(&apple_dma_pci_info);
}

type_init(apple_dma_pci_register)
