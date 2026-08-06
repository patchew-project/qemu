/*
 * graphics passthrough
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/xen/xen_pt.h"
#include "hw/xen/xen_igd.h"
#include "xen-host-pci-device.h"
#include "system/physmem.h"

static unsigned long igd_guest_opregion;
static unsigned long igd_host_opregion;

/*
 * These are true until they are set to false when the guest first
 * accesses the OpRegion address register for a read or write,
 * respectively.
 */
static bool first_guest_opregion_read = true;
static bool first_guest_opregion_write = true;

static uint32_t guest_opregion_extra_writes;
static bool guest_supports_opregion2;
static bool done;
static unsigned long rvda; /* absolute host VBT address */
static unsigned long vbt_guest_pgbase;
static uint32_t vbt_nr_pages;

#define XEN_PCI_INTEL_OPREGION_MASK 0xfff
#define XEN_PCI_INTEL_OPREGION_PAGES 0x3
#define XEN_PCI_INTEL_OPREGION_ENABLE_ACCESSED 0x1
#define XEN_PCI_INTEL_OPREGION_DISABLE_ACCESS 0x0
#define XEN_PCI_INTEL_OPREGION2_SUPPORT_MASK 0x1

typedef struct VGARegion {
    int type;           /* Memory or port I/O */
    uint64_t guest_base_addr;
    uint64_t machine_base_addr;
    uint64_t size;    /* size of the region */
    int rc;
} VGARegion;

#define IORESOURCE_IO           0x00000100
#define IORESOURCE_MEM          0x00000200

static struct VGARegion vga_args[] = {
    {
        .type = IORESOURCE_IO,
        .guest_base_addr = 0x3B0,
        .machine_base_addr = 0x3B0,
        .size = 0xC,
        .rc = -1,
    },
    {
        .type = IORESOURCE_IO,
        .guest_base_addr = 0x3C0,
        .machine_base_addr = 0x3C0,
        .size = 0x20,
        .rc = -1,
    },
    {
        .type = IORESOURCE_MEM,
        .guest_base_addr = 0xa0000 >> XC_PAGE_SHIFT,
        .machine_base_addr = 0xa0000 >> XC_PAGE_SHIFT,
        .size = 0x20,
        .rc = -1,
    },
};

/*
 * register VGA resources for the domain with assigned gfx
 */
int xen_pt_register_vga_regions(XenHostPCIDevice *dev)
{
    int i = 0;

    if (!is_igd_vga_passthrough(dev)) {
        return 0;
    }

    for (i = 0 ; i < ARRAY_SIZE(vga_args); i++) {
        if (vga_args[i].type == IORESOURCE_IO) {
            vga_args[i].rc = xc_domain_ioport_mapping(xen_xc, xen_domid,
                            vga_args[i].guest_base_addr,
                            vga_args[i].machine_base_addr,
                            vga_args[i].size, DPCI_ADD_MAPPING);
        } else {
            vga_args[i].rc = xc_domain_memory_mapping(xen_xc, xen_domid,
                            vga_args[i].guest_base_addr,
                            vga_args[i].machine_base_addr,
                            vga_args[i].size, DPCI_ADD_MAPPING);
        }

        if (vga_args[i].rc) {
            XEN_PT_ERR(NULL, "VGA %s mapping failed! (rc: %i)\n",
                    vga_args[i].type == IORESOURCE_IO ? "ioport" : "memory",
                    vga_args[i].rc);
            return vga_args[i].rc;
        }
    }

    return 0;
}

/*
 * unregister VGA resources for the domain with assigned gfx
 */
int xen_pt_unregister_vga_regions(XenHostPCIDevice *dev)
{
    int i = 0;
    int ret = 0;

    if (!is_igd_vga_passthrough(dev)) {
        return 0;
    }

    for (i = 0 ; i < ARRAY_SIZE(vga_args); i++) {
        if (vga_args[i].type == IORESOURCE_IO) {
            vga_args[i].rc = xc_domain_ioport_mapping(xen_xc, xen_domid,
                            vga_args[i].guest_base_addr,
                            vga_args[i].machine_base_addr,
                            vga_args[i].size, DPCI_REMOVE_MAPPING);
        } else {
            vga_args[i].rc = xc_domain_memory_mapping(xen_xc, xen_domid,
                            vga_args[i].guest_base_addr,
                            vga_args[i].machine_base_addr,
                            vga_args[i].size, DPCI_REMOVE_MAPPING);
        }

        if (vga_args[i].rc) {
            XEN_PT_ERR(NULL, "VGA %s unmapping failed! (rc: %i)\n",
                    vga_args[i].type == IORESOURCE_IO ? "ioport" : "memory",
                    vga_args[i].rc);
            return vga_args[i].rc;
        }
    }

    if (!guest_supports_opregion2 && igd_guest_opregion) {
        ret = xc_domain_memory_mapping(xen_xc, xen_domid,
                (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT),
                (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
                XEN_PCI_INTEL_OPREGION_PAGES,
                DPCI_REMOVE_MAPPING);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

static void *get_vgabios(XenPCIPassthroughState *s, int *size,
                       XenHostPCIDevice *dev)
{
    return pci_assign_dev_load_option_rom(&s->dev, size,
                                          dev->domain, dev->bus,
                                          dev->dev, dev->func);
}

/* Refer to Seabios. */
struct rom_header {
    uint16_t signature;
    uint8_t size;
    uint8_t initVector[4];
    uint8_t reserved[17];
    uint16_t pcioffset;
    uint16_t pnpoffset;
} __attribute__((packed));

struct pci_data {
    uint32_t signature;
    uint16_t vendor;
    uint16_t device;
    uint16_t vitaldata;
    uint16_t dlen;
    uint8_t drevision;
    uint8_t class_lo;
    uint16_t class_hi;
    uint16_t ilen;
    uint16_t irevision;
    uint8_t type;
    uint8_t indicator;
    uint16_t reserved;
} __attribute__((packed));

void xen_pt_setup_vga(XenPCIPassthroughState *s, XenHostPCIDevice *dev,
                     Error **errp)
{
    unsigned char *bios = NULL;
    struct rom_header *rom;
    int bios_size;
    char *c = NULL;
    char checksum = 0;
    uint32_t len = 0;
    struct pci_data *pd = NULL;

    if (!is_igd_vga_passthrough(dev)) {
        error_setg(errp, "Need to enable igd-passthrough");
        return;
    }

    bios = get_vgabios(s, &bios_size, dev);
    if (!bios) {
        error_setg(errp, "VGA: Can't get VBIOS");
        return;
    }

    /* Case when the host ROM file from sysfs could not be read */
    if (!bios_size) {
        object_unparent(OBJECT(&s->dev.rom));
        bios = NULL;
        return;
    }

    if (bios_size < sizeof(struct rom_header)) {
        error_setg(errp, "VGA: VBIOS image corrupt (too small)");
        return;
    }

    /* Currently we fixed this address as a primary. */
    rom = (struct rom_header *)bios;

    if (rom->pcioffset + sizeof(struct pci_data) > bios_size) {
        error_setg(errp, "VGA: VBIOS image corrupt (bad pcioffset field)");
        return;
    }

    pd = (void *)(bios + (unsigned char)rom->pcioffset);

    /* We may need to fixup Device Identification. */
    if (pd->device != s->real_device.device_id) {
        pd->device = s->real_device.device_id;

        len = rom->size * 512;
        if (len > bios_size) {
            error_setg(errp, "VGA: VBIOS image corrupt (bad size field)");
            return;
        }

        /* Then adjust the bios checksum */
        for (c = (char *)bios; c < ((char *)bios + len); c++) {
            checksum += *c;
        }
        if (checksum) {
            bios[len - 1] -= checksum;
            XEN_PT_LOG(&s->dev, "vga bios checksum is adjusted %x!\n",
                       checksum);
        }
    }

    pci_register_bar(&s->dev, PCI_ROM_SLOT, 0, &s->dev.rom);
    s->dev.has_rom = true;

    /* Currently we fixed this address as a primary for legacy BIOS. */
    physical_memory_write(0xc0000, bios, bios_size);
}

uint32_t igd_read_opregion(XenPCIPassthroughState *s)
{
    if (!igd_host_opregion) {
        /* We just work with LE. */
        xen_host_pci_get_block(&s->real_device, XEN_PCI_INTEL_OPREGION,
                               (uint8_t *)&igd_host_opregion, 4);
    }

    /*
     * By returning igd_host_opregion here instead of 0, we can
     * indicate to hvmloader that we support OpRegion 2.
     *
     * The conditions are there to prevent returning igd_host_opregion
     * to guests that have a version of hvmloader that lacks support
     * for OpRegion 2. We do this to maintain backward compatibility for
     * guests with earlier versions of hvmloader that always expect us
     * to return 0 instead of igd_host_opregion when igd_guest_opregion
     * is not yet set to a non-zero value.
     */
    if (first_guest_opregion_read && !igd_guest_opregion &&
        first_guest_opregion_write) {
        first_guest_opregion_read = false;
        return igd_host_opregion;
    }

    uint32_t val = 0;
    first_guest_opregion_read = false;

    if (!igd_guest_opregion) {
        return val;
    }

    val = igd_guest_opregion;

    XEN_PT_LOG(&s->dev, "Read opregion val=%x\n", val);
    return val;
}

void igd_write_opregion(XenPCIPassthroughState *s, uint32_t val)
{
    int ret;

    /* hvmloader with OpRegion 2 support uses lsb of val to indicate support */
    if ((val & XEN_PCI_INTEL_OPREGION2_SUPPORT_MASK) &&
        first_guest_opregion_write) {
        guest_supports_opregion2 = true;
    } else if (first_guest_opregion_write) {
        XEN_PT_LOG(&s->dev, "hvmloader lacks extended VBT support, "
                   "continuing with legacy support only\n");
    }

    if ((!guest_supports_opregion2 && igd_guest_opregion) || done) {
        XEN_PT_LOG(&s->dev, "opregion register already been set, ignoring %x\n",
                   val);
        return;
    }

    if (guest_supports_opregion2 && !first_guest_opregion_write) {
        /*
         * OpRegion 2 is supported and we are processing
         * additional writes that the legacy protocol ignores.
         *
         * We should always return from this if block to prevent
         * executing code below which is only for the first write
         * when we map the host OpRegion into the guest.
         */
        guest_opregion_extra_writes++;
        switch (guest_opregion_extra_writes) {
        case 1:
            /*
             * Hvmloader expects us to store the value as the least
             * significant DWORD of rvda.
             */
            rvda = (unsigned long)val;
            break;
        case 2:
            /*
             * Hvmloader expects us to store the value as the most
             * significant DWORD of rvda and unmap the OpRegion if
             * rvda is not equal to zero.
             *
             * If the unmapping fails, hvmloader will fall back to the
             * behavior of older versions which simply map the OpRegion
             * from the host to the guest without trying to configure
             * the guest with OpRegion 2 with extended VBT support.
             */
            rvda |= (unsigned long)(val) << 32;
            if (rvda) {
                ret = xc_domain_memory_mapping(xen_xc, xen_domid,
                                               (unsigned long)
                                               (igd_guest_opregion >> XC_PAGE_SHIFT),
                                               (unsigned long)
                                               (igd_host_opregion >> XC_PAGE_SHIFT),
                                               XEN_PCI_INTEL_OPREGION_PAGES,
                                               DPCI_REMOVE_MAPPING);
                if (ret) {
                    XEN_PT_ERR(&s->dev, "[%d]:Can't unmap IGD host opregion:0x%lx"
                               " from guest opregion:0x%lx.\n", ret,
                               (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
                               (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT));
                    rvda = 0;
                    guest_supports_opregion2 = false;
                }
                ret = xc_domain_iomem_permission(xen_xc, xen_domid,
                                                 (unsigned long)
                                                 (igd_host_opregion >> XC_PAGE_SHIFT),
                                                 XEN_PCI_INTEL_OPREGION_PAGES,
                                                 XEN_PCI_INTEL_OPREGION_DISABLE_ACCESS);
                if (ret) {
                    XEN_PT_WARN(&s->dev, "[%d]:Can't disable access to IGD host"
                                " OpRegion: 0x%x.\n", ret,
                                (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT));
                }
            } else {
                guest_supports_opregion2 = false;
            }
            break;
        case 3:
            /*
             * Hvmloader expects us to store the value as the address
             * to map the VBT to in the guest and to map the VBT at the
             * provided address in the guest. Hvmloader encodes the number
             * of pages to map in the least significant 12 bits of the
             * provided address.
             *
             * If VBT verification fails, hvmloader can't determine if the
             * VBT is mapped but corrupted or unmapped, so it crashes the
             * guest as an unrecoverable error.
             */

            /* address (gfn) to map VBT to in the guest */
            vbt_guest_pgbase = val >> XC_PAGE_SHIFT;
            vbt_nr_pages = val & XEN_PCI_INTEL_OPREGION_MASK;
            ret = xc_domain_iomem_permission(xen_xc, xen_domid,
                                             (unsigned long)(rvda >> XC_PAGE_SHIFT),
                                             vbt_nr_pages,
                                             XEN_PCI_INTEL_OPREGION_ENABLE_ACCESSED);
            if (ret) {
                XEN_PT_ERR(&s->dev, "[%d]:Can't enable access to IGD host VBT:"
                           " 0x%lx.\n", ret,
                           (unsigned long)(rvda >> XC_PAGE_SHIFT)),
                rvda = 0;
                vbt_guest_pgbase = 0;
                vbt_nr_pages = 0;
                done = true;
                break;
            }
            ret = xc_domain_memory_mapping(xen_xc, xen_domid,
                                           (unsigned long)vbt_guest_pgbase,
                                           (unsigned long)(rvda >> XC_PAGE_SHIFT),
                                           vbt_nr_pages, DPCI_ADD_MAPPING);
            if (ret) {
                XEN_PT_ERR(&s->dev, "[%d]:Can't map IGD host VBT:0x%lx to"
                           " guest VBT:0x%lx.\n", ret,
                           (unsigned long)(rvda >> XC_PAGE_SHIFT),
                           (unsigned long)vbt_guest_pgbase);
                rvda = 0;
                vbt_guest_pgbase = 0;
                vbt_nr_pages = 0;
                done = true;
                break;
            }
            XEN_PT_LOG(&s->dev, "Map VBT: 0x%lx -> 0x%lx\n",
                       (unsigned long)(rvda >> XC_PAGE_SHIFT),
                       (unsigned long)vbt_guest_pgbase);
            XEN_PT_LOG(&s->dev, "VBT host address: 0x%lx\n", rvda);
            break;
        case 4:
            /*
             * Hvmloader expects us to store the given value as the
             * final value for the register that stores the OpRegion
             * address in the guest. We also unmap the VBT since the
             * guest now has its own copy of both it and the OpRegion.
             *
             * If the unmapping fails the VBT will be mapped where
             * hvmloader needs to place the OpRegion plus VBT in the
             * guest E820 map. In this case, hvmloader will crash with
             * BUG() rather than try to use the mapped VBT with the
             * guest's copy of the OpRegion.
             */
            igd_guest_opregion = val;
            ret = xc_domain_memory_mapping(xen_xc, xen_domid,
                                           (unsigned long)vbt_guest_pgbase,
                                           (unsigned long)(rvda >> XC_PAGE_SHIFT),
                                           vbt_nr_pages, DPCI_REMOVE_MAPPING);
            if (ret) {
                XEN_PT_ERR(&s->dev, "[%d]:Can't unmap IGD host VBT:0x%lx from"
                           " guest VBT:0x%lx.\n", ret,
                           (unsigned long)(rvda >> XC_PAGE_SHIFT),
                           (unsigned long)vbt_guest_pgbase);
                rvda = 0;
                done = true;
                break;
            }

            ret = xc_domain_iomem_permission(xen_xc, xen_domid,
                                             (unsigned long)(rvda >> XC_PAGE_SHIFT),
                                             vbt_nr_pages,
                                             XEN_PCI_INTEL_OPREGION_DISABLE_ACCESS);
            if (ret) {
                XEN_PT_WARN(&s->dev, "[%d]:Can't disable access to IGD host"
                            " VBT: 0x%x.\n", ret,
                            (unsigned long)(rvda >> XC_PAGE_SHIFT));
            }

            done = true;
            break;
        default:
            break;
        }
        return;
    }

    /*
     * This code handles the first write to the register from the guest.
     * It maps the host OpRegion into the guest.
     *
     * Set first_guest_opregion_write to false to enable more writes
     * if OpRegion 2 is supported.
     */
    first_guest_opregion_write = false;

    if (!igd_host_opregion) {
        /* We just work with LE. */
        xen_host_pci_get_block(&s->real_device, XEN_PCI_INTEL_OPREGION,
                               (uint8_t *)&igd_host_opregion, 4);
    }
    igd_guest_opregion = (unsigned long)(val & ~XEN_PCI_INTEL_OPREGION_MASK)
                            | (igd_host_opregion & XEN_PCI_INTEL_OPREGION_MASK);

    ret = xc_domain_iomem_permission(xen_xc, xen_domid,
            (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
            XEN_PCI_INTEL_OPREGION_PAGES,
            XEN_PCI_INTEL_OPREGION_ENABLE_ACCESSED);

    if (ret) {
        XEN_PT_ERR(&s->dev, "[%d]:Can't enable to access IGD host opregion:"
                    " 0x%lx.\n", ret,
                    (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT)),
        igd_guest_opregion = 0;
        return;
    }

    ret = xc_domain_memory_mapping(xen_xc, xen_domid,
            (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT),
            (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
            XEN_PCI_INTEL_OPREGION_PAGES,
            DPCI_ADD_MAPPING);

    if (ret) {
        XEN_PT_ERR(&s->dev, "[%d]:Can't map IGD host opregion:0x%lx to"
                    " guest opregion:0x%lx.\n", ret,
                    (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
                    (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT));
        igd_guest_opregion = 0;
        return;
    }

    XEN_PT_LOG(&s->dev, "Map OpRegion: 0x%lx -> 0x%lx\n",
                    (unsigned long)(igd_host_opregion >> XC_PAGE_SHIFT),
                    (unsigned long)(igd_guest_opregion >> XC_PAGE_SHIFT));
}

typedef struct {
    uint16_t gpu_device_id;
    uint16_t pch_device_id;
    uint8_t pch_revision_id;
} IGDDeviceIDInfo;

/*
 * In real world different GPU should have different PCH. But actually
 * the different PCH DIDs likely map to different PCH SKUs. We do the
 * same thing for the GPU. For PCH, the different SKUs are going to be
 * all the same silicon design and implementation, just different
 * features turn on and off with fuses. The SW interfaces should be
 * consistent across all SKUs in a given family (eg LPT). But just same
 * features may not be supported.
 *
 * Most of these different PCH features probably don't matter to the
 * Gfx driver, but obviously any difference in display port connections
 * will so it should be fine with any PCH in case of passthrough.
 *
 * So currently use one PCH version, 0x8c4e, to cover all HSW(Haswell)
 * scenarios, 0x9cc3 for BDW(Broadwell).
 */
static const IGDDeviceIDInfo igd_combo_id_infos[] = {
    /* HSW Classic */
    {0x0402, 0x8c4e, 0x04}, /* HSWGT1D, HSWD_w7 */
    {0x0406, 0x8c4e, 0x04}, /* HSWGT1M, HSWM_w7 */
    {0x0412, 0x8c4e, 0x04}, /* HSWGT2D, HSWD_w7 */
    {0x0416, 0x8c4e, 0x04}, /* HSWGT2M, HSWM_w7 */
    {0x041E, 0x8c4e, 0x04}, /* HSWGT15D, HSWD_w7 */
    /* HSW ULT */
    {0x0A06, 0x8c4e, 0x04}, /* HSWGT1UT, HSWM_w7 */
    {0x0A16, 0x8c4e, 0x04}, /* HSWGT2UT, HSWM_w7 */
    {0x0A26, 0x8c4e, 0x06}, /* HSWGT3UT, HSWM_w7 */
    {0x0A2E, 0x8c4e, 0x04}, /* HSWGT3UT28W, HSWM_w7 */
    {0x0A1E, 0x8c4e, 0x04}, /* HSWGT2UX, HSWM_w7 */
    {0x0A0E, 0x8c4e, 0x04}, /* HSWGT1ULX, HSWM_w7 */
    /* HSW CRW */
    {0x0D26, 0x8c4e, 0x04}, /* HSWGT3CW, HSWM_w7 */
    {0x0D22, 0x8c4e, 0x04}, /* HSWGT3CWDT, HSWD_w7 */
    /* HSW Server */
    {0x041A, 0x8c4e, 0x04}, /* HSWSVGT2, HSWD_w7 */
    /* HSW SRVR */
    {0x040A, 0x8c4e, 0x04}, /* HSWSVGT1, HSWD_w7 */
    /* BSW */
    {0x1606, 0x9cc3, 0x03}, /* BDWULTGT1, BDWM_w7 */
    {0x1616, 0x9cc3, 0x03}, /* BDWULTGT2, BDWM_w7 */
    {0x1626, 0x9cc3, 0x03}, /* BDWULTGT3, BDWM_w7 */
    {0x160E, 0x9cc3, 0x03}, /* BDWULXGT1, BDWM_w7 */
    {0x161E, 0x9cc3, 0x03}, /* BDWULXGT2, BDWM_w7 */
    {0x1602, 0x9cc3, 0x03}, /* BDWHALOGT1, BDWM_w7 */
    {0x1612, 0x9cc3, 0x03}, /* BDWHALOGT2, BDWM_w7 */
    {0x1622, 0x9cc3, 0x03}, /* BDWHALOGT3, BDWM_w7 */
    {0x162B, 0x9cc3, 0x03}, /* BDWHALO28W, BDWM_w7 */
    {0x162A, 0x9cc3, 0x03}, /* BDWGT3WRKS, BDWM_w7 */
    {0x162D, 0x9cc3, 0x03}, /* BDWGT3SRVR, BDWM_w7 */
};

static void isa_bridge_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->desc        = "ISA bridge faked to support IGD PT";
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    k->vendor_id    = PCI_VENDOR_ID_INTEL;
    k->class_id     = PCI_CLASS_BRIDGE_ISA;
};

static const TypeInfo isa_bridge_info = {
    .name          = "igd-passthrough-isa-bridge",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = isa_bridge_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pt_graphics_register_types(void)
{
    type_register_static(&isa_bridge_info);
}
type_init(pt_graphics_register_types)

static void xen_pt_get_host_pch_info(uint16_t *pch_dev_id, uint8_t *pch_rev_id,
                                     Error **errp)
{
    g_autofree XenHostPCIDevice *pch_dev = g_new(XenHostPCIDevice, 1);

    xen_host_pci_device_get(pch_dev, 0, 0, 0x1f, 0, errp);
    if (*errp) {
        goto error;
    }

    *pch_dev_id = pch_dev->device_id;

    if (xen_host_pci_get_byte(pch_dev, PCI_REVISION_ID, pch_rev_id)) {
        *pch_rev_id = 0x1;
        warn_report("IGD: failed to get host PCH revision, setting it to 0x1");
    }

    xen_host_pci_device_put(pch_dev);
    return;

error:
    error_append_hint(errp, "failed to get host PCH device for Intel IGD");
}

void xen_igd_passthrough_isa_bridge_create(XenPCIPassthroughState *s,
                                           XenHostPCIDevice *dev,
                                           Error **errp)
{
    PCIBus *bus = pci_get_bus(&s->dev);
    struct PCIDevice *bridge_dev;
    int i, num;
    const uint16_t gpu_dev_id = dev->device_id;
    uint16_t pch_dev_id = 0xffff;
    uint8_t pch_rev_id = 0;

    num = ARRAY_SIZE(igd_combo_id_infos);
    for (i = 0; i < num; i++) {
        if (gpu_dev_id == igd_combo_id_infos[i].gpu_device_id) {
            pch_dev_id = igd_combo_id_infos[i].pch_device_id;
            pch_rev_id = igd_combo_id_infos[i].pch_revision_id;
        }
    }

    /* Newer devices get PCH infos from host sysfs */
    if ((pch_dev_id == 0xffff) || !pch_rev_id) {
        xen_pt_get_host_pch_info(&pch_dev_id, &pch_rev_id, errp);
    }

    XEN_PT_LOG(&s->dev, "PCH device id: 0x%x\n", pch_dev_id);
    XEN_PT_LOG(&s->dev, "PCH revision: 0x%x\n", pch_rev_id);

    if (pch_dev_id == 0xffff) {
        error_setg(errp, "failed to get PCH device id");
        return;
    }

    /* Currently IGD drivers always need to access PCH by 1f.0. */
    bridge_dev = pci_create_simple(bus, PCI_DEVFN(0x1f, 0),
                                   "igd-passthrough-isa-bridge");

    /*
     * Note that vendor id is always PCI_VENDOR_ID_INTEL.
     */
    if (!bridge_dev) {
        error_setg(errp, "set igd-passthrough-isa-bridge failed!");
        return;
    }
    pci_config_set_device_id(bridge_dev->config, pch_dev_id);
    pci_config_set_revision(bridge_dev->config, pch_rev_id);
}
