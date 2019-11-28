/*
 * QEMU R300 SVGA emulation
 *
 * Copyright (c) 2019 Aaron Zakhrov
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */



#ifndef R300_H
#define R300_H

#include "qemu/timer.h"
#include "hw/pci/pci.h"
#include "hw/i2c/bitbang_i2c.h"
#include "vga_int.h"

/*#define DEBUG_R300*/

#ifdef DEBUG_R300
#define DPRINTF(fmt, ...) printf("%s: " fmt, __func__, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define PCI_VENDOR_ID_ATI 0x1002
/* Radeon 9500 PRO */
#define PCI_DEVICE_ID_ATI_RADEON_9500_PRO 0x4e45
/* Radeon 9500 PRO */
#define PCI_DEVICE_ID_ATI_RADEON_9700 0x4e44

#define RADEON_MIN_MMIO_SIZE 0x10000


#define TYPE_RAD_VGA "rad-vga"
#define RAD_VGA(obj) OBJECT_CHECK(RADVGAState, (obj), TYPE_RAD_VGA)

typedef struct RADVGARegs{

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
  uint32_t dp_cntl;
  uint32_t dp_datatype;
  uint32_t dp_mix;
  uint32_t dp_write_mask;
  uint32_t default_offset;
  uint32_t default_pitch;
  uint32_t default_tile;
  uint32_t default_sc_bottom_right;
  uint32_t mc_status;
  uint32_t isync_cntl;
  uint32_t host_path_cntl;
  uint32_t wait_until;
  uint32_t cp_csq_cntl;
  uint32_t scratch_umask;
  uint32_t r100_display_base_addr;
  uint32_t r100_sclk_cntl;
  uint32_t pcie_index;
  uint32_t pcie_data;
    uint32_t aic_lo_addr;
      uint32_t aic_hi_addr;
      uint32_t fp_gen_cntl;
      uint32_t mm_data;



  uint8_t vga_reset;
  uint32_t tile_x0_y0;
  uint32_t dda_config;
  uint32_t aic_cntl;

  uint32_t cp_rb_cntl;
  uint32_t mem_cntl;


  uint32_t surface_cntl;
  uint32_t surface0_info;
  uint32_t surface1_info;
  uint32_t surface2_info;
  uint32_t surface3_info;
  uint32_t surface4_info;
  uint32_t surface5_info;
  uint32_t surface6_info;
  uint32_t surface7_info;
  uint32_t ov0_scale_cntl;
  uint32_t i2c_cntl_1;
  uint32_t dvi_i2c_cntl_1;
  uint32_t subpic_cntl;
  uint32_t viph_control;
  uint32_t cap0_trig_cntl;
  uint32_t cap1_trig_cntl;
  uint32_t cur2_offset;

  uint32_t crtc2_gen_cntl;

  uint32_t mem_intf_cntl;
  uint32_t agp_base_2;
  uint32_t agp_base;

  uint32_t mem_addr_config;
  uint32_t display2_base_addr;
  uint32_t spll_cntl;
  uint32_t vclk_ecp_cntl;

  uint32_t aic_pt_base;
  uint32_t pci_gart_page;
  uint32_t mc_agp_location;






  //R300 DST registers

  uint32_t r300_dst_pipe_config;

  //R300 GB Registers
  uint32_t r300_gb_enable;
  uint32_t r300_gb_tile_config;
  uint32_t r300_gb_fifo_size;
  uint32_t r300_gb_select;
  uint32_t r300_gb_aa_config;
  uint32_t r300_gb_mpos_0;
  uint32_t r300_gb_mpos_1;

  // RE registers
  uint32_t r300_re_scissors_tl;
  uint32_t r300_re_scissors_br;

  // RB2D registers
uint32_t r300_rb2d_dstcache_mode;

// RB3D Registers
  uint32_t r300_rb3d_aaresolve_ctl;
  uint32_t r300_rb3d_aaresolve_offset;
  uint32_t r300_rb3d_aaresolve_pitch;
  uint32_t r300_rb3d_ablend;
  uint32_t r300_rb3d_blend_color;
  uint32_t r300_rb3d_cblend;
  uint32_t r300_rb3d_color_mask;
  uint32_t r300_rb3d_color_pitch[4];
  uint32_t r300_rb3d_color_offset[4];
  uint32_t r300_rb3d_zcache_ctlstat;
  uint32_t r300_rb3d_dstcache_ctlstat;

  uint32_t rbbm_gui_cntl;
  uint32_t rbbm_status;
  uint32_t rbbm_soft_reset;

  uint32_t emu_register_stub[1024];

  //PLL CLK REGISTERS
uint32_t m_spll_ref_fb_div;




// MC registers
  uint32_t r300_mc_init_gfx_lat_timer;
  uint32_t r300_mc_init_misc_lat_timer;

//SE registers
  uint32_t r300_se_vport_xscale;
  uint32_t r300_se_vport_xoffset;
  uint32_t r300_se_vport_yscale;
  uint32_t r300_se_vport_yoffset;
  uint32_t r300_se_vport_zscale;
  uint32_t r300_se_vport_zoffset;
  uint32_t r300_se_vte_cntl;

//VAP registers

uint32_t r300_vap_cntl;
uint32_t r300_vap_cntl_status;
uint32_t r300_vap_output_vtx_fmt_0;
uint32_t r300_vap_output_vtx_fmt_1;
uint32_t r300_vap_input_cntl_0;
uint32_t r300_vap_input_cntl_1;
uint32_t r300_vap_input_route_0_0;
uint32_t r300_vap_input_route_0_1;
uint32_t r300_vap_input_route_0_2;
uint32_t r300_vap_input_route_0_3;
uint32_t r300_vap_input_route_0_4;
uint32_t r300_vap_input_route_0_5;
uint32_t r300_vap_input_route_0_6;
uint32_t r300_vap_input_route_0_7;
uint32_t r300_vap_input_route_1_0;
uint32_t r300_vap_input_route_1_1;
uint32_t r300_vap_input_route_1_2;
uint32_t r300_vap_input_route_1_3;
uint32_t r300_vap_input_route_1_4;
uint32_t r300_vap_input_route_1_5;
uint32_t r300_vap_input_route_1_6;
uint32_t r300_vap_input_route_1_7;
uint32_t r300_vap_pvs_upload_address;
uint32_t r300_vap_pvs_upload_data;





}RADVGARegs;
typedef struct RADVGAState {
    PCIDevice dev;
    VGACommonState vga;
    char *model;
    uint16_t dev_id;
    uint8_t mode;
    bool cursor_guest_mode;
    uint16_t cursor_size;
    uint32_t cursor_offset;
    QEMUCursor *cursor;
    QEMUTimer vblank_timer;
    bitbang_i2c_interface bbi2c;
    MemoryRegion io;
    MemoryRegion mm;
    MemoryRegion gart;
    AddressSpace gart_as;
    RADVGARegs regs;
} RADVGAState;

const char *r300_reg_name(int num);

void r300_2d_blt(RADVGAState *s);




#endif
