/*
 * ASPEED VGA display controller
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The display controller found in the ASPEED BMC SoCs is exposed to the
 * host as a PCI VGA device that is register compatible with the IBM VGA
 * 1.0 specification, extended with a set of vendor specific registers.
 *
 * - BAR 0: linear framebuffer (VRAM)
 * - BAR 1: SoC register window; the mirror of the legacy VGA I/O ports
 *   at offset 0x380 is the only implemented part
 * - legacy VGA I/O ports and the 0xa0000 memory window, so that a
 *   plain VGA BIOS keeps working before the ast driver takes over
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/pci/pci_device.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "ui/console.h"
#include "trace.h"
#include "aspeed-vga.h"
#include "vga_int.h"
#include "vga_regs.h"

/*
 * VGA Display Controller (VGA)
 *
 * Offset 0 from the BAR 1 register base
 */

/* General Registers */
REG8(VGAER, 0x3c3)
    FIELD(VGAER, VGA_ENABLE, 0, 1)

/*
 * ASPEED extended CRT controller register indices
 *
 * The CRT controller is reached through its index and data port pair,
 * 0x3b4/0x3b5 in mono mode and 0x3d4/0x3d5 in color mode.
 */
REG8(AST_CR_PASSWORD, 0x80)
#define AST_CR_PASSWORD_UNLOCK  0xa8
REG8(AST_CR_VRAM_RSRV, 0x99)
REG8(AST_CR_PCI_CTRL2, 0xa1)
    FIELD(AST_CR_PCI_CTRL2, MMIO_ENABLED, 2, 1)
REG8(AST_CR_COLOR_MODE, 0xa3)
    FIELD(AST_CR_COLOR_MODE, FORMAT, 0, 4)
#define AST_CR_COLOR_MODE_FORMAT_32BPP    BIT(3)
#define AST_CR_COLOR_MODE_FORMAT_16BPP    BIT(2)
#define AST_CR_COLOR_MODE_FORMAT_15BPP    BIT(1)
#define AST_CR_COLOR_MODE_FORMAT_8BPP     BIT(0)
REG8(AST_CR_STRAP1, 0xaa)
REG8(AST_CR_H_OVERFLOW, 0xac)
    FIELD(AST_CR_H_OVERFLOW, HDE, 2, 2)
REG8(AST_CR_V_OVERFLOW, 0xae)
    FIELD(AST_CR_V_OVERFLOW, VDE_D10, 1, 1)
REG8(AST_CR_START_ADDR_EXT, 0xaf)
REG8(AST_CR_OFFSET_HI, 0xb0)
    FIELD(AST_CR_OFFSET_HI, OFFSET, 0, 6)
REG8(AST_CR_POWER_MGMT, 0xb6)
    FIELD(AST_CR_POWER_MGMT, VSYNC_OFF, 1, 1)
    FIELD(AST_CR_POWER_MGMT, HSYNC_OFF, 0, 1)
REG8(AST_CR_SOC_SCRATCH0, 0xd0)
    FIELD(AST_CR_SOC_SCRATCH0, VRAM_INIT_BY_BMC, 7, 1)
    FIELD(AST_CR_SOC_SCRATCH0, VRAM_INIT_READY, 6, 1)
    FIELD(AST_CR_SOC_SCRATCH0, IKVM_WIDESCREEN, 0, 1)

/*
 * The ast driver writes a color format to CRA3 when it switches to the
 * extended mode. Until then CRA3 is zero and the device behaves like a
 * standard VGA.
 */
static bool aspeed_vga_ext_enabled(AspeedVGAState *s)
{
    uint8_t format = FIELD_EX8(s->vga.cr[R_AST_CR_COLOR_MODE],
                               AST_CR_COLOR_MODE, FORMAT);

    return format != 0;
}

static int aspeed_vga_get_bpp(VGACommonState *vga)
{
    AspeedVGAState *s = container_of(vga, AspeedVGAState, vga);
    uint8_t format;

    if (!aspeed_vga_ext_enabled(s)) {
        return s->std_get_bpp(vga);
    }

    format = FIELD_EX8(vga->cr[R_AST_CR_COLOR_MODE],
                       AST_CR_COLOR_MODE, FORMAT);

    switch (format) {
    case AST_CR_COLOR_MODE_FORMAT_8BPP:
        return 8;
    case AST_CR_COLOR_MODE_FORMAT_15BPP:
        return 15;
    case AST_CR_COLOR_MODE_FORMAT_16BPP:
        return 16;
    case AST_CR_COLOR_MODE_FORMAT_32BPP:
        return 32;
    default:
        return 0;
    }
}

/*
 * The ast driver turns the display off through CR17 bit 7 or the power
 * management register, not through the attribute controller bit that the
 * common VGA code looks at.
 */
static bool aspeed_vga_is_blanked(VGACommonState *vga)
{
    AspeedVGAState *s = container_of(vga, AspeedVGAState, vga);
    bool crtc_sync_off;
    bool pm_sync_off;

    if (!aspeed_vga_ext_enabled(s)) {
        return false;
    }

    crtc_sync_off = !(vga->cr[VGA_CRTC_MODE] & VGA_CR17_H_V_SIGNALS_ENABLED);
    pm_sync_off = vga->cr[R_AST_CR_POWER_MGMT] &
                  (R_AST_CR_POWER_MGMT_HSYNC_OFF_MASK |
                   R_AST_CR_POWER_MGMT_VSYNC_OFF_MASK);

    return crtc_sync_off || pm_sync_off;
}

static void aspeed_vga_get_resolution(VGACommonState *vga,
                                      int *pwidth, int *pheight)
{
    AspeedVGAState *s = container_of(vga, AspeedVGAState, vga);
    int height;
    int width;

    if (!aspeed_vga_ext_enabled(s)) {
        s->std_get_resolution(vga, pwidth, pheight);
        return;
    }

    /* horizontal display end, D[7:0] in CR01 and D[9:8] in CRAC */
    width = vga->cr[VGA_CRTC_H_DISP] |
            (FIELD_EX8(vga->cr[R_AST_CR_H_OVERFLOW],
                       AST_CR_H_OVERFLOW, HDE) << 8);
    width = (width + 1) * 8;

    /*
     * vertical display end, D[7:0] in CR12, D[9:8] in CR07 and
     * D[10] in CRAE
     */
    height = vga->cr[VGA_CRTC_V_DISP_END] |
             ((vga->cr[VGA_CRTC_OVERFLOW] & 0x02) << 7) |
             ((vga->cr[VGA_CRTC_OVERFLOW] & 0x40) << 3);
    if (FIELD_EX8(vga->cr[R_AST_CR_V_OVERFLOW], AST_CR_V_OVERFLOW, VDE_D10)) {
        height |= BIT(10);
    }
    height = height + 1;

    *pwidth = width;
    *pheight = height;
}

static void aspeed_vga_get_params(VGACommonState *vga,
                                  VGADisplayParams *params)
{
    AspeedVGAState *s = container_of(vga, AspeedVGAState, vga);

    if (!aspeed_vga_ext_enabled(s)) {
        s->std_get_params(vga, params);
        return;
    }

    /*
     * GR05 bit 6 selects the mode 13 shift mode, which is packed pixel like
     * the extended modes. Without it the common code picks its planar
     * reader when it draws into its own surface.
     */
    vga->gr[VGA_GFX_MODE] |= BIT(6);

    /*
     * line offset is in units of 8 bytes, D[7:0] in CR13 and
     * D[13:8] in CRB0
     */
    params->line_offset = (vga->cr[VGA_CRTC_OFFSET] |
                           (FIELD_EX8(vga->cr[R_AST_CR_OFFSET_HI],
                                      AST_CR_OFFSET_HI, OFFSET) << 8)) * 8;

    /*
     * start address is in units of 4 bytes, D[7:0] in CR0D,
     * D[15:8] in CR0C and D[23:16] in CRAF
     */
    params->start_addr = vga->cr[VGA_CRTC_START_LO] |
                         (vga->cr[VGA_CRTC_START_HI] << 8) |
                         (vga->cr[R_AST_CR_START_ADDR_EXT] << 16);

    params->line_compare = 65535;
    params->hpel = VGA_HPEL_NEUTRAL;
    params->hpel_split = false;
}

static uint8_t aspeed_vga_vgamem_size_reg(uint32_t vram_size_mb)
{
    switch (vram_size_mb) {
    case 8:
        return 0;
    case 16:
        return 1;
    case 32:
        return 2;
    case 64:
        return 3;
    default:
        g_assert_not_reached();
    }
}

static uint8_t aspeed_vga_read_byte(AspeedVGAState *s, uint32_t port)
{
    uint8_t val;

    switch (port) {
    case A_VGAER:
        val = s->vgaer;
        break;
    default:
        val = vga_ioport_read(&s->vga, port);
        break;
    }

    trace_aspeed_vga_read_byte(port, val);
    return val;
}

static void aspeed_vga_write_byte(AspeedVGAState *s, uint32_t port, uint8_t val)
{
    trace_aspeed_vga_write_byte(port, val);

    switch (port) {
    case A_VGAER:
        s->vgaer = val & R_VGAER_VGA_ENABLE_MASK;
        break;
    default:
        vga_ioport_write(&s->vga, port, val);
        break;
    }
}

static uint64_t aspeed_vga_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    AspeedVGAState *s = opaque;
    uint64_t val = 0;
    unsigned i;

    addr += ASPEED_VGA_IOPORT_OFFSET;

    for (i = 0; i < size; i++) {
        val = deposit64(val, i * 8, 8, aspeed_vga_read_byte(s, addr + i));
    }

    return val;
}

static void aspeed_vga_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    AspeedVGAState *s = opaque;
    unsigned i;

    addr += ASPEED_VGA_IOPORT_OFFSET;

    /*
     * Byte by byte in little endian order, so that a single word write to an
     * index/data register pair updates the index first.
     */
    for (i = 0; i < size; i++) {
        aspeed_vga_write_byte(s, addr + i, extract64(val, i * 8, 8));
    }
}

static const MemoryRegionOps aspeed_vga_ioport_ops = {
    .read = aspeed_vga_ioport_read,
    .write = aspeed_vga_ioport_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void aspeed_vga_reset_hold(Object *obj, ResetType type)
{
    AspeedVGAState *s = ASPEED_VGA(obj);
    VGACommonState *vga = &s->vga;

    vga_common_reset(vga);

    s->vgaer = R_VGAER_VGA_ENABLE_MASK;

    /*
     * Select the color emulation I/O addresses, so that the CRTC answers at
     * 0x3d4/0x3d5 where the extended registers live. The VGA BIOS normally
     * does this, and the ast driver only does it when it has to POST the
     * chip, which this model reports as already done.
     */
    vga->msr = VGA_MIS_COLOR;

    vga->cr[R_AST_CR_PASSWORD] = AST_CR_PASSWORD_UNLOCK;
    vga->cr[R_AST_CR_PCI_CTRL2] = R_AST_CR_PCI_CTRL2_MMIO_ENABLED_MASK;

    /* VRAM size, no reserved area */
    vga->cr[R_AST_CR_STRAP1] = aspeed_vga_vgamem_size_reg(vga->vram_size_mb);
    vga->cr[R_AST_CR_VRAM_RSRV] = 0;

    vga->cr[R_AST_CR_SOC_SCRATCH0] =
        R_AST_CR_SOC_SCRATCH0_VRAM_INIT_BY_BMC_MASK |
        R_AST_CR_SOC_SCRATCH0_VRAM_INIT_READY_MASK |
        R_AST_CR_SOC_SCRATCH0_IKVM_WIDESCREEN_MASK;
}

/*
 * The standard VGA has a bochs VBE interface and this hardware does not, so
 * set up the 0xa0000 window and the legacy I/O ports here, not vga_init().
 */
static void aspeed_vga_init_io(AspeedVGAState *s, PCIDevice *dev)
{
    MemoryRegion *address_space = pci_address_space(dev);
    const MemoryRegionPortio *vga_ports;
    const MemoryRegionPortio *vbe_ports;
    VGACommonState *vga = &s->vga;
    MemoryRegion *vga_io_memory;

    vga->bank_offset = 0;
    vga->legacy_address_space = address_space;

    vga_io_memory = vga_init_io(vga, OBJECT(dev), &vga_ports, &vbe_ports);
    memory_region_add_subregion_overlap(address_space, 0x000a0000,
                                        vga_io_memory, 1);
    memory_region_set_coalescing(vga_io_memory);

    /* vga_init_io() fills in both VGA and VBE ports; only VGA is registered */
    portio_list_init(&vga->vga_port_list, OBJECT(dev), vga_ports, vga, "vga");
    portio_list_set_flush_coalesced(&vga->vga_port_list);
    portio_list_add(&vga->vga_port_list, pci_address_space_io(dev), 0x3b0);
}

static void aspeed_vga_realize(PCIDevice *dev, Error **errp)
{
    AspeedVGAClass *ac = ASPEED_VGA_GET_CLASS(dev);
    AspeedVGAState *s = ASPEED_VGA(dev);
    VGACommonState *vga = &s->vga;

    switch (vga->vram_size_mb) {
    case 8:
    case 16:
    case 32:
    case 64:
        break;
    default:
        error_setg(errp,
                   TYPE_ASPEED_VGA ": vgamem_mb must be 8, 16, 32 or 64");
        return;
    }

    if (!vga_common_init(vga, OBJECT(dev), errp)) {
        return;
    }

    aspeed_vga_init_io(s, dev);

    s->std_get_bpp = vga->get_bpp;
    s->std_get_params = vga->get_params;
    s->std_get_resolution = vga->get_resolution;
    vga->get_bpp = aspeed_vga_get_bpp;
    vga->get_params = aspeed_vga_get_params;
    vga->get_resolution = aspeed_vga_get_resolution;
    vga->is_blanked = aspeed_vga_is_blanked;
    vga->big_endian_fb = false;

    vga->con = qemu_graphic_console_create(DEVICE(dev), 0, vga->hw_ops, vga);

    pci_set_byte(&dev->config[PCI_REVISION_ID], ac->revision);

    memory_region_init_io(&s->mmio, OBJECT(dev), &unassigned_io_ops, NULL,
                          "aspeed-vga.container", ASPEED_VGA_MMIO_SIZE);

    memory_region_init_io(&s->ioport, OBJECT(dev), &aspeed_vga_ioport_ops, s,
                          "aspeed-vga.ioport", ASPEED_VGA_IOPORT_SIZE);
    memory_region_add_subregion(&s->mmio, ASPEED_VGA_IOPORT_OFFSET,
                                &s->ioport);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &vga->vram);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void aspeed_vga_exit(PCIDevice *dev)
{
    AspeedVGAState *s = ASPEED_VGA(dev);

    qemu_graphic_console_close(s->vga.con);
}

static const VMStateDescription vmstate_aspeed_vga = {
    .name = "aspeed-vga",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, AspeedVGAState),
        VMSTATE_STRUCT(vga, AspeedVGAState, 0, vmstate_vga_common,
                       VGACommonState),
        VMSTATE_UINT8(vgaer, AspeedVGAState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property aspeed_vga_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", AspeedVGAState, vga.vram_size_mb, 32),
};

static void aspeed_vga_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = aspeed_vga_realize;
    k->exit = aspeed_vga_exit;
    k->vendor_id = PCI_VENDOR_ID_ASPEED;
    k->device_id = 0x2000;
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    k->subsystem_vendor_id = PCI_VENDOR_ID_ASPEED;
    k->subsystem_id = 0x2000;
    k->romfile = "vgabios-stdvga.bin";

    dc->vmsd = &vmstate_aspeed_vga;
    rc->phases.hold = aspeed_vga_reset_hold;
    device_class_set_props(dc, aspeed_vga_properties);
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void ast2600_vga_class_init(ObjectClass *klass, const void *data)
{
    AspeedVGAClass *avc = ASPEED_VGA_CLASS(klass);

    DEVICE_CLASS(klass)->desc = "ASPEED AST2600 VGA";
    avc->revision = 0x52;
}

static void ast2700_vga_class_init(ObjectClass *klass, const void *data)
{
    AspeedVGAClass *avc = ASPEED_VGA_CLASS(klass);

    DEVICE_CLASS(klass)->desc = "ASPEED AST2700 VGA";
    avc->revision = 0x72;
}

static const TypeInfo aspeed_vga_types[] = {
    {
        .name          = TYPE_ASPEED_VGA,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(AspeedVGAState),
        .class_size    = sizeof(AspeedVGAClass),
        .class_init    = aspeed_vga_class_init,
        .abstract      = true,
        .interfaces = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    }, {
        .name          = TYPE_AST2600_VGA,
        .parent        = TYPE_ASPEED_VGA,
        .class_init    = ast2600_vga_class_init,
    }, {
        .name          = TYPE_AST2700_VGA,
        .parent        = TYPE_ASPEED_VGA,
        .class_init    = ast2700_vga_class_init,
    },
};

DEFINE_TYPES(aspeed_vga_types)
