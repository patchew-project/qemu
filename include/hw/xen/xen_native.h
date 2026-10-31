#ifndef QEMU_HW_XEN_NATIVE_H
#define QEMU_HW_XEN_NATIVE_H

#ifdef __XEN_INTERFACE_VERSION__
#error In Xen native files, include xen_native.h before other Xen headers
#endif

/*
 * If we have new enough libxenctrl then we do not want/need these compat
 * interfaces, despite what the user supplied cflags might say. They
 * must be undefined before including xenctrl.h
 */
#undef XC_WANT_COMPAT_EVTCHN_API
#undef XC_WANT_COMPAT_GNTTAB_API
#undef XC_WANT_COMPAT_MAP_FOREIGN_API

#include <xenctrl.h>
#include <xenstore.h>

#include "hw/xen/xen.h"
#include "hw/pci/pci_device.h"
#include "hw/xen/trace.h"

extern xc_interface *xen_xc;

/*
 * We don't support Xen prior to 4.11.0.
 */

#include <xenforeignmemory.h>

extern xenforeignmemory_handle *xen_fmem;

#undef XC_WANT_COMPAT_DEVICEMODEL_API
#include <xendevicemodel.h>

#include <xentoolcore.h>

extern xendevicemodel_handle *xen_dmod;

static inline int xen_set_mem_type(domid_t domid, hvmmem_type_t type,
                                   uint64_t first_pfn, uint32_t nr)
{
    return xendevicemodel_set_mem_type(xen_dmod, domid, type, first_pfn,
                                       nr);
}

static inline int xen_set_pci_intx_level(domid_t domid, uint16_t segment,
                                         uint8_t bus, uint8_t device,
                                         uint8_t intx, unsigned int level)
{
    return xendevicemodel_set_pci_intx_level(xen_dmod, domid, segment, bus,
                                             device, intx, level);
}

static inline int xen_inject_msi(domid_t domid, uint64_t msi_addr,
                                 uint32_t msi_data)
{
    return xendevicemodel_inject_msi(xen_dmod, domid, msi_addr, msi_data);
}

static inline int xen_set_isa_irq_level(domid_t domid, uint8_t irq,
                                        unsigned int level)
{
    return xendevicemodel_set_isa_irq_level(xen_dmod, domid, irq, level);
}

static inline int xen_track_dirty_vram(domid_t domid, uint64_t first_pfn,
                                       uint32_t nr, unsigned long *bitmap)
{
    return xendevicemodel_track_dirty_vram(xen_dmod, domid, first_pfn, nr,
                                           bitmap);
}

static inline int xen_modified_memory(domid_t domid, uint64_t first_pfn,
                                      uint32_t nr)
{
    return xendevicemodel_modified_memory(xen_dmod, domid, first_pfn, nr);
}

static inline int xen_restrict(domid_t domid)
{
    int rc;
    rc = xentoolcore_restrict_all(domid);
    trace_xen_domid_restrict(rc ? errno : 0);
    return rc;
}

void destroy_hvm_domain(bool reboot);

/* shutdown/destroy current domain because of an error */
void xen_shutdown_fatal_error(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

#ifdef HVM_PARAM_VMPORT_REGS_PFN
static inline int xen_get_vmport_regs_pfn(xc_interface *xc, domid_t dom,
                                          xen_pfn_t *vmport_regs_pfn)
{
    int rc;
    uint64_t value;
    rc = xc_hvm_param_get(xc, dom, HVM_PARAM_VMPORT_REGS_PFN, &value);
    if (rc >= 0) {
        *vmport_regs_pfn = (xen_pfn_t) value;
    }
    return rc;
}
#else
static inline int xen_get_vmport_regs_pfn(xc_interface *xc, domid_t dom,
                                          xen_pfn_t *vmport_regs_pfn)
{
    return -ENOSYS;
}
#endif

static inline int xen_get_default_ioreq_server_info(domid_t dom,
                                                    xen_pfn_t *ioreq_pfn,
                                                    xen_pfn_t *bufioreq_pfn,
                                                    evtchn_port_t
                                                        *bufioreq_evtchn)
{
    unsigned long param;
    int rc;

    rc = xc_get_hvm_param(xen_xc, dom, HVM_PARAM_IOREQ_PFN, &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_IOREQ_PFN\n");
        return -1;
    }

    *ioreq_pfn = param;

    rc = xc_get_hvm_param(xen_xc, dom, HVM_PARAM_BUFIOREQ_PFN, &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_BUFIOREQ_PFN\n");
        return -1;
    }

    *bufioreq_pfn = param;

    rc = xc_get_hvm_param(xen_xc, dom, HVM_PARAM_BUFIOREQ_EVTCHN,
                          &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_BUFIOREQ_EVTCHN\n");
        return -1;
    }

    *bufioreq_evtchn = param;

    return 0;
}

static bool use_default_ioreq_server;

static inline void xen_map_memory_section(domid_t dom,
                                          ioservid_t ioservid,
                                          MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_map_mmio_range(ioservid, start_addr, end_addr);
    xendevicemodel_map_io_range_to_ioreq_server(xen_dmod, dom, ioservid, 1,
                                                start_addr, end_addr);
}

static inline void xen_unmap_memory_section(domid_t dom,
                                            ioservid_t ioservid,
                                            MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_unmap_mmio_range(ioservid, start_addr, end_addr);
    xendevicemodel_unmap_io_range_from_ioreq_server(xen_dmod, dom, ioservid,
                                                    1, start_addr, end_addr);
}

static inline void xen_map_io_section(domid_t dom,
                                      ioservid_t ioservid,
                                      MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_map_portio_range(ioservid, start_addr, end_addr);
    xendevicemodel_map_io_range_to_ioreq_server(xen_dmod, dom, ioservid, 0,
                                                start_addr, end_addr);
}

static inline void xen_unmap_io_section(domid_t dom,
                                        ioservid_t ioservid,
                                        MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_unmap_portio_range(ioservid, start_addr, end_addr);
    xendevicemodel_unmap_io_range_from_ioreq_server(xen_dmod, dom, ioservid,
                                                    0, start_addr, end_addr);
}

static inline void xen_map_pcidev(domid_t dom,
                                  ioservid_t ioservid,
                                  PCIDevice *pci_dev)
{
    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_map_pcidev(ioservid, pci_dev_bus_num(pci_dev),
                         PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
    xendevicemodel_map_pcidev_to_ioreq_server(xen_dmod, dom, ioservid, 0,
                                              pci_dev_bus_num(pci_dev),
                                              PCI_SLOT(pci_dev->devfn),
                                              PCI_FUNC(pci_dev->devfn));
}

static inline void xen_unmap_pcidev(domid_t dom,
                                    ioservid_t ioservid,
                                    PCIDevice *pci_dev)
{
    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_unmap_pcidev(ioservid, pci_dev_bus_num(pci_dev),
                           PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
    xendevicemodel_unmap_pcidev_from_ioreq_server(xen_dmod, dom, ioservid, 0,
                                                  pci_dev_bus_num(pci_dev),
                                                  PCI_SLOT(pci_dev->devfn),
                                                  PCI_FUNC(pci_dev->devfn));
}

static inline int xen_create_ioreq_server(domid_t dom,
                                          int handle_bufioreq,
                                          ioservid_t *ioservid)
{
    int rc = xendevicemodel_create_ioreq_server(xen_dmod, dom,
                                                handle_bufioreq,
                                                ioservid);

    if (rc == 0) {
        trace_xen_ioreq_server_create(*ioservid);
        return rc;
    }

    *ioservid = 0;
    use_default_ioreq_server = true;
    trace_xen_default_ioreq_server();

    return rc;
}

static inline void xen_destroy_ioreq_server(domid_t dom,
                                            ioservid_t ioservid)
{
    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_ioreq_server_destroy(ioservid);
    xendevicemodel_destroy_ioreq_server(xen_dmod, dom, ioservid);
}

static inline int xen_get_ioreq_server_info(domid_t dom,
                                            ioservid_t ioservid,
                                            xen_pfn_t *ioreq_pfn,
                                            xen_pfn_t *bufioreq_pfn,
                                            evtchn_port_t *bufioreq_evtchn)
{
    if (use_default_ioreq_server) {
        return xen_get_default_ioreq_server_info(dom, ioreq_pfn,
                                                 bufioreq_pfn,
                                                 bufioreq_evtchn);
    }

    return xendevicemodel_get_ioreq_server_info(xen_dmod, dom, ioservid,
                                                ioreq_pfn, bufioreq_pfn,
                                                bufioreq_evtchn);
}

static inline int xen_set_ioreq_server_state(domid_t dom,
                                             ioservid_t ioservid,
                                             bool enable)
{
    if (use_default_ioreq_server) {
        return 0;
    }

    trace_xen_ioreq_server_state(ioservid, enable);
    return xendevicemodel_set_ioreq_server_state(xen_dmod, dom, ioservid,
                                                 enable);
}

#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 41500
static inline int xendevicemodel_set_irq_level(xendevicemodel_handle *dmod,
                                               domid_t domid, uint32_t irq,
                                               unsigned int level)
{
    return -1;
}
#endif

#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 41700
#define GUEST_VIRTIO_MMIO_BASE   xen_mk_ullong(0x02000000)
#define GUEST_VIRTIO_MMIO_SIZE   xen_mk_ullong(0x00100000)
#define GUEST_VIRTIO_MMIO_SPI_FIRST   33
#define GUEST_VIRTIO_MMIO_SPI_LAST    43
#endif

#if defined(__i386__) || defined(__x86_64__)
#define GUEST_RAM_BANKS   2
#define GUEST_RAM0_BASE   0x40000000ULL /* 3GB of low RAM @ 1GB */
#define GUEST_RAM0_SIZE   0xc0000000ULL
#define GUEST_RAM1_BASE   0x0200000000ULL /* 1016GB of RAM @ 8GB */
#define GUEST_RAM1_SIZE   0xfe00000000ULL
#endif

#endif /* QEMU_HW_XEN_NATIVE_H */
