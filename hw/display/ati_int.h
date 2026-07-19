/*
 * QEMU ATI SVGA emulation
 *
 * Copyright (c) 2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#ifndef ATI_INT_H
#define ATI_INT_H

#include "qemu/timer.h"
#include "qemu/units.h"
#include "hw/pci/pci_device.h"
#include "hw/i2c/bitbang_i2c.h"
#include "hw/display/i2c-ddc.h"
#include "vga_int.h"
#include "qom/object.h"

/*#define DEBUG_ATI*/

#ifdef DEBUG_ATI
#define DPRINTF(fmt, ...) printf("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define PCI_VENDOR_ID_ATI 0x1002
/* Rage128 Pro GL */
#define PCI_DEVICE_ID_ATI_RAGE128_PF 0x5046
/* Radeon RV100 (VE) */
#define PCI_DEVICE_ID_ATI_RADEON_QY 0x5159

#define ATI_RAGE128_LINEAR_APER_SIZE (64 * MiB)
#define ATI_R100_LINEAR_APER_SIZE (128 * MiB)
#define ATI_HOST_DATA_ACC_BITS 128

#define TYPE_ATI_VGA "ati-vga"
OBJECT_DECLARE_SIMPLE_TYPE(ATIVGAState, ATI_VGA)

#define ATI_PKT_TYPE_MASK            0xc0000000
#define ATI_PKT_TYPE_SHIFT           30

#define ATI_PKT_TYPE0                0
#define ATI_PKT_TYPE0_BASE_REG_MASK  0x00007fff
#define ATI_PKT_TYPE0_BASE_REG_SHIFT 0
#define ATI_PKT_TYPE0_ONE_REG_WR     0x00008000
#define ATI_PKT_TYPE0_COUNT_MASK     0x3fff0000
#define ATI_PKT_TYPE0_COUNT_SHIFT    16

#define ATI_PKT_TYPE1                1
#define ATI_PKT_TYPE1_REG0_MASK      0x000007ff
#define ATI_PKT_TYPE1_REG0_SHIFT     0
#define ATI_PKT_TYPE1_REG1_MASK      0x003ff800
#define ATI_PKT_TYPE1_REG1_SHIFT     11

#define ATI_PKT_TYPE2                2

#define ATI_PKT_TYPE3                3
#define ATI_PKT_TYPE3_OPCODE_MASK    0x0000ff00
#define ATI_PKT_TYPE3_OPCODE_SHIFT   8
#define ATI_PKT_TYPE3_COUNT_MASK     0x3fff0000
#define ATI_PKT_TYPE3_COUNT_SHIFT    16

typedef struct ATIVGARegs {
    uint32_t mm_index;
    uint32_t bios_scratch[8];
    uint32_t gen_int_cntl;
    uint32_t gen_int_status;
    uint32_t crtc_gen_cntl;
    uint32_t crtc_ext_cntl;
    uint32_t dac_cntl;
    uint32_t gpio_vga_ddc;
    uint32_t gpio_dvi_ddc;
    uint32_t gpio_monid;
    uint32_t config_cntl;
    uint32_t palette[256];
    uint32_t crtc_h_total_disp;
    uint32_t crtc_h_sync_strt_wid;
    uint32_t crtc_v_total_disp;
    uint32_t crtc_v_sync_strt_wid;
    uint32_t crtc_offset;
    uint32_t crtc_offset_cntl;
    uint32_t crtc_pitch;
    uint32_t cur_offset;
    uint32_t cur_hv_pos;
    uint32_t cur_hv_offs;
    uint32_t cur_color0;
    uint32_t cur_color1;
    uint32_t dst_offset;
    uint32_t dst_pitch;
    uint32_t dst_tile;
    uint32_t dst_width;
    uint32_t dst_height;
    uint32_t src_offset;
    uint32_t src_pitch;
    uint32_t src_tile;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t dst_x;
    uint32_t dst_y;
    uint32_t dp_gui_master_cntl;
    uint32_t dp_brush_bkgd_clr;
    uint32_t dp_brush_frgd_clr;
    uint32_t dp_src_frgd_clr;
    uint32_t dp_src_bkgd_clr;
    uint16_t sc_top;
    uint16_t sc_left;
    uint16_t sc_bottom;
    uint16_t sc_right;
    uint16_t src_sc_bottom;
    uint16_t src_sc_right;
    uint32_t dp_cntl;
    uint32_t dp_datatype;
    uint32_t dp_mix;
    uint32_t dp_write_mask;
    uint32_t default_offset;
    uint32_t default_pitch;
    uint16_t default_sc_bottom;
    uint16_t default_sc_right;
    uint32_t default_tile;
    uint32_t aic_ctrl;
    uint32_t aic_pt_base;
    uint32_t aic_lo_addr;
    uint32_t aic_hi_addr;
    uint32_t mc_fb_location;
} ATIVGARegs;

typedef struct ATIHostDataState {
    bool active;
    uint32_t row;
    uint32_t col;
    uint32_t next;
    uint32_t acc[4];
} ATIHostDataState;

typedef struct ATIType0Header {
    uint32_t base_reg;
    uint16_t count;
    bool one_reg_wr;
} ATIType0Header;

typedef struct ATIType1Header {
    uint32_t reg0;
    uint32_t reg1;
} ATIType1Header;

/* Type-2 headers are a no-op and have no state */

typedef struct ATIType3Header {
    uint8_t opcode;
    uint16_t count;
} ATIType3Header;

typedef struct ATIPktState {
    uint8_t type;
    uint16_t dwords_processed;
    union {
        ATIType0Header t0;
        ATIType1Header t1;
        ATIType3Header t3;
    };
} ATIPktState;

struct ATIVGAState {
    PCIDevice dev;
    VGACommonState vga;
    char *model;
    uint16_t dev_id;
    uint8_t mode;
    uint8_t use_pixman;
    bool cursor_guest_mode;
    uint16_t cursor_size;
    uint32_t cursor_offset;
    QEMUCursor *cursor;
    QEMUTimer vblank_timer;
    bitbang_i2c_interface bbi2c;
    I2CDDCState i2cddc;
    uint64_t linear_aper_sz;
    MemoryRegion linear_aper;
    MemoryRegion io;
    MemoryRegion mm;
    ATIVGARegs regs;
    ATIHostDataState host_data;
    ATIPktState cur_packet;
};

typedef struct {
    bool is_vram;
    hwaddr addr;
} ATIMemRoute;

const char *ati_reg_name(int num);

void ati_mm_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size);
ATIMemRoute ati_mc_route(ATIVGAState *s, uint32_t gpu_addr);
void ati_2d_blt(ATIVGAState *s);
bool ati_host_data_flush(ATIVGAState *s);
void ati_host_data_finish(ATIVGAState *s);
void ati_pkt_receive_data(ATIVGAState *s, ATIPktState *p, uint32_t data);

#endif /* ATI_INT_H */
