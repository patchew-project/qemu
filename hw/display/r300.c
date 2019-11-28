/*
 * QEMU ATI R300 SVGA emulation
 *
 * Copyright (c) 2019 Aaron Zakhrov
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

/*
 * WARNING:
 * This is very incomplete and only enough for Linux console and some
 * unaccelerated X output at the moment.
 * Currently it's little more than a frame buffer with minimal functions,
 * other more advanced features of the hardware are yet to be implemented.
 * We only aim for Rage 128 Pro (and some RV100) and 2D only at first,
 * No 3D at all yet (maybe after 2D works, but feel free to improve it)
 */

#include "qemu/osdep.h"
#include "r300.h"
#include "radeon_reg.h"
#include "r100d.h"
#include "r300d.h"
#include "vga-access.h"
#include "hw/qdev-properties.h"
#include "vga_regs.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "hw/display/i2c-ddc.h"
#include "trace.h"
#include "hw/i386/amd_iommu.h"

#define R300_DEBUG_HW_CURSOR 0

static const struct {
    const char *name;
    uint16_t dev_id;
} r300_model_aliases[] = {
    { "radeon9500", PCI_DEVICE_ID_ATI_RADEON_9500_PRO },
    { "radeon9700", PCI_DEVICE_ID_ATI_RADEON_9700 }
};
enum { VGA_MODE, EXT_MODE };




static void r300_vga_update_irq(RADVGAState *s)
{
    pci_set_irq(&s->dev, !!(s->regs.gen_int_status & s->regs.gen_int_cntl));
}

static void r300_vga_vblank_irq(void *opaque)
{
    RADVGAState *s = opaque;

    timer_mod(&s->vblank_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              NANOSECONDS_PER_SECOND / 60);
    s->regs.gen_int_status |= RADEON_CRTC_VBLANK_CUR ;
    r300_vga_update_irq(s);
}

static inline uint64_t r300_reg_read_offs(uint32_t reg, int offs,
                                         unsigned int size)
{
    if (offs == 0 && size == 4) {
        return reg;
    } else {
        return extract32(reg, offs * BITS_PER_BYTE, size * BITS_PER_BYTE);
    }
}

static uint64_t r300_mm_read(void *opaque, hwaddr addr, unsigned int size)
{
    RADVGAState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
      // case RADEON_MCLK_CNTL:
      // val = RADEON_FORCEON_MCLKA;
      // break;
    case RADEON_MC_STATUS:
        val = s->regs.mc_status;
        break;

    case RADEON_MM_INDEX:
        val = s->regs.mm_index;
        break;
    case RADEON_MM_DATA:

        val = s->regs.mm_data;
        break;
        case RADEON_BIOS_0_SCRATCH:
        {
          val =s->regs.bios_scratch[0];
            break;
        }
        case RADEON_BIOS_1_SCRATCH:
        {
          val =s->regs.bios_scratch[1];
            break;
        }
        case RADEON_BIOS_2_SCRATCH:
        {
          val =s->regs.bios_scratch[2];
            break;
        }
        case RADEON_BIOS_3_SCRATCH:
        {
          val =s->regs.bios_scratch[3];
            break;
        }
        case RADEON_BIOS_4_SCRATCH:
        {
          val =s->regs.bios_scratch[4];
            break;
        }
        case RADEON_BIOS_5_SCRATCH:
        {
          val =s->regs.bios_scratch[5];
            break;
        }
        case RADEON_BIOS_6_SCRATCH:
        {
          val =s->regs.bios_scratch[6];
            break;
        }
        case RADEON_BIOS_7_SCRATCH:
        {
          val =s->regs.bios_scratch[7];
            break;
        }
    case RADEON_GEN_INT_CNTL:
        val = s->regs.gen_int_cntl;

        break;
    case RADEON_GEN_INT_STATUS:
        val = s->regs.gen_int_status;
        break;
    case RADEON_CRTC_GEN_CNTL:
          val = vga_ioport_read(s,addr);
        break;
    case RADEON_CRTC_EXT_CNTL:
    val = vga_ioport_read(s,addr);
    break;
    case RADEON_GPIO_VGA_DDC:

        val = s->regs.gpio_vga_ddc;
        break;
    case RADEON_GPIO_DVI_DDC:
      val = s->regs.gpio_dvi_ddc;
      break;
    case RADEON_CONFIG_CNTL:
        val = s->regs.config_cntl;
        break;
    case RADEON_CONFIG_MEMSIZE:
        val = s->vga.vram_size;
        qemu_log("RADEON_MEMSIZE %ld \n",val);
        break;
    case RADEON_CONFIG_APER_SIZE:
        val = s->vga.vram_size;
        break;

    case RADEON_RBBM_STATUS:
        val = 64; /* free CMDFIFO entries */
        break;
    case RADEON_CRTC_H_TOTAL_DISP:
        val = s->regs.crtc_h_total_disp;
        break;
    case RADEON_CRTC_H_SYNC_STRT_WID:
        val = s->regs.crtc_h_sync_strt_wid;
        break;
    case RADEON_CRTC_V_TOTAL_DISP:
        val = s->regs.crtc_v_total_disp;
        break;
    case RADEON_CRTC_V_SYNC_STRT_WID:
        val = s->regs.crtc_v_sync_strt_wid;
        break;
    case RADEON_CRTC_OFFSET:
        val = s->regs.crtc_offset;
        break;
    case RADEON_CRTC_OFFSET_CNTL:
        val = s->regs.crtc_offset_cntl;
        break;
    case RADEON_CRTC_PITCH:
        val = s->regs.crtc_pitch;
        break;
    case RADEON_CUR_OFFSET:
        val = s->regs.cur_offset;
        break;
    case RADEON_CUR_HORZ_VERT_POSN:
        val = s->regs.cur_hv_pos;
        val |= s->regs.cur_offset & BIT(31);
        break;
    case RADEON_CUR_HORZ_VERT_OFF:
        val = s->regs.cur_hv_offs;
        val |= s->regs.cur_offset & BIT(31);
        break;
    case RADEON_CUR_CLR0:
        val = s->regs.cur_color0;
        break;
    case RADEON_CUR_CLR1:
        val = s->regs.cur_color1;
        break;
    case RADEON_DST_OFFSET:
        val = s->regs.dst_offset;
        break;
    case RADEON_DST_PITCH:
        val = s->regs.dst_pitch;

        break;
    case RADEON_DST_WIDTH:
        val = s->regs.dst_width;
        break;
    case RADEON_DST_HEIGHT:
        val = s->regs.dst_height;
        break;
    case RADEON_SRC_X:
        val = s->regs.src_x;
        break;
    case RADEON_SRC_Y:
        val = s->regs.src_y;
        break;
    case RADEON_DST_X:
        val = s->regs.dst_x;
        break;
    case RADEON_DST_Y:
        val = s->regs.dst_y;
        break;
    case RADEON_DP_GUI_MASTER_CNTL:
        val = s->regs.dp_gui_master_cntl;
        break;
    case RADEON_SRC_OFFSET:
        val = s->regs.src_offset;
        break;
    case RADEON_SRC_PITCH:
        val = s->regs.src_pitch;
        break;
    case RADEON_DP_BRUSH_BKGD_CLR:
        val = s->regs.dp_brush_bkgd_clr;
        break;
    case RADEON_DP_BRUSH_FRGD_CLR:
        val = s->regs.dp_brush_frgd_clr;
        break;
    case RADEON_DP_SRC_FRGD_CLR:
        val = s->regs.dp_src_frgd_clr;
        break;
    case RADEON_DP_SRC_BKGD_CLR:
        val = s->regs.dp_src_bkgd_clr;
        break;
    case RADEON_DP_CNTL:
        val = s->regs.dp_cntl;
        break;
    case RADEON_DP_DATATYPE:
        val = s->regs.dp_datatype;
        break;
    case RADEON_DP_MIX:
        val = s->regs.dp_mix;
        break;
    case RADEON_DP_WRITE_MASK:
        val = s->regs.dp_write_mask;
        break;
    case RADEON_DEFAULT_OFFSET:
        val = s->regs.default_offset;
        break;
    case RADEON_DEFAULT_PITCH:
        val = s->regs.default_pitch;
        val |= s->regs.default_tile << 16;
        break;
    case RADEON_DEFAULT_SC_BOTTOM_RIGHT:
        val = s->regs.default_sc_bottom_right;
        break;
        case R300_GB_ENABLE:
            val = s->regs.r300_gb_enable;
            break;
        case R300_GB_TILE_CONFIG:
                val= s->regs.r300_gb_tile_config;
                break;
        case R300_GB_FIFO_SIZE:
                val =s->regs.r300_gb_fifo_size ;
                break;
        case RADEON_ISYNC_CNTL:
                val =s->regs.isync_cntl;
                break;
      case R300_DST_PIPE_CONFIG:
                val =s->regs.r300_dst_pipe_config;
                break;
      case R300_RB2D_DSTCACHE_MODE:
                val = s->regs.r300_rb2d_dstcache_mode;
                break;
      case RADEON_WAIT_UNTIL:
                val = s->regs.wait_until;
                break;
      case R300_GB_SELECT:
                val = s->regs.r300_gb_select;
                break;
      case R300_RB3D_DSTCACHE_CTLSTAT:
                val = s->regs.r300_rb3d_dstcache_ctlstat;
                break;
      case R300_RB3D_ZCACHE_CTLSTAT:
                val = s->regs.r300_rb3d_zcache_ctlstat;
                break;
      case R300_GB_AA_CONFIG:
                val = s->regs.r300_gb_aa_config;
                break;
      case R300_RE_SCISSORS_TL:
                val = s->regs.r300_re_scissors_tl;
                break;

      case R300_RE_SCISSORS_BR:
                val = s->regs.r300_re_scissors_br;
                break;
      case RADEON_HOST_PATH_CNTL:
                val = s->regs.host_path_cntl;
                break;

      case R300_GB_MSPOS0:
                val = s->regs.r300_gb_mpos_0;
                break;

      case R300_GB_MSPOS1:
                val = s->regs.r300_gb_mpos_1;
                break;

                case RADEON_SURFACE_CNTL:
                          val = s->regs.surface_cntl;
                          break;
                case RADEON_SURFACE0_INFO:
                          val = s->regs.surface0_info;
                          break;
                case RADEON_SURFACE1_INFO:
                          val = s->regs.surface1_info;
                          break;

                case RADEON_SURFACE2_INFO:
                val = s->regs.surface2_info;
                break;
                case RADEON_SURFACE3_INFO:
                val = s->regs.surface3_info;
                break;
                case RADEON_SURFACE4_INFO:
                val = s->regs.surface4_info;
                break;
                case RADEON_SURFACE5_INFO:
                val = s->regs.surface5_info;
                break;
                case RADEON_SURFACE6_INFO:
                val = s->regs.surface6_info;
                break;
                case RADEON_SURFACE7_INFO:
                val = s->regs.surface7_info;
                break;
                case RADEON_OV0_SCALE_CNTL:
                val = s->regs.ov0_scale_cntl;
                break;
                case RADEON_SUBPIC_CNTL:
                val = s->regs.subpic_cntl;
                break;
                case RADEON_VIPH_CONTROL:
                val = s->regs.viph_control;
                break;
                case RADEON_I2C_CNTL_1:
                val = s->regs.i2c_cntl_1;
                break;
                case RADEON_DVI_I2C_CNTL_1:
                val = s->regs.dvi_i2c_cntl_1;
                break;
                case RADEON_CAP0_TRIG_CNTL:
                val = s->regs.cap0_trig_cntl;
                break;
                case RADEON_CAP1_TRIG_CNTL:
                val = s->regs.cap1_trig_cntl;
                break;
                case RADEON_CUR2_OFFSET:
                val = s->regs.cur2_offset;
                break;
                case RADEON_CRTC2_GEN_CNTL:
                val = s->regs.crtc2_gen_cntl;
                break;
                case RADEON_AGP_BASE_2:
                val = s->regs.agp_base_2;
                break;
                case RADEON_AGP_BASE:
                val = s->regs.agp_base;
                break;
                case RADEON_MEM_ADDR_CONFIG:
                val = s->regs.mem_addr_config;
                break;
                case RADEON_DISPLAY2_BASE_ADDR:
                val = s->regs.r100_display_base_addr;
                break;
                case RADEON_SPLL_CNTL:
                val = s->regs.spll_cntl;
                break;
                case RADEON_VCLK_ECP_CNTL:
                val = s->regs.vclk_ecp_cntl;
                break;


                case RADEON_GENMO_WT:

                      break;
                case RADEON_CP_CSQ_CNTL:
                    val = s->regs.cp_csq_cntl;
                      break;
                case RADEON_SCRATCH_UMSK:
                      val = s->regs.scratch_umask;
                      break;
                case RADEON_SCLK_CNTL:
                        val= s->regs.r100_sclk_cntl;
                        qemu_log("RADEON_SCLK 0x%08lx \n",val);

                        break;
                case R_00023C_DISPLAY_BASE_ADDR:
                        val = s->regs.r100_display_base_addr;
                        break;
                case RADEON_MEM_CNTL:
                    val = R300_MEM_NUM_CHANNELS_MASK & R300_MEM_USE_CD_CH_ONLY;
                    break;
                case RADEON_CP_RB_CNTL:
                    val = RADEON_RB_NO_UPDATE;
                    break;

                    case R300_CRTC_TILE_X0_Y0:
                    val = s->regs.tile_x0_y0;

                    break;
                    case R300_MC_INIT_MISC_LAT_TIMER:
                    val = s->regs.r300_mc_init_misc_lat_timer;
                    break;

                    case RADEON_M_SPLL_REF_FB_DIV:
                    val = s->regs.m_spll_ref_fb_div;
                    break;
                    case RADEON_AIC_CNTL:
                    val = s->regs.aic_cntl;
                    break;
                    case RADEON_AIC_PT_BASE:
                    val = s->regs.aic_pt_base;

                    break;

                    case RADEON_PCI_GART_PAGE:
                    qemu_log("READ GART \n");
                    val = s->regs.pci_gart_page;
                    qemu_log("GART REGISTER 0x%08lx CONTAINS 0x%08lx \n",addr,val);
                    break;
                    case RADEON_MC_AGP_LOCATION:
                    val = s->regs.mc_agp_location;
                    // qemu_log("READ MC_AGP \n");
                    break;
                    case RADEON_PCIE_INDEX:
                    val = s->regs.pcie_index;
                    break;
                    case RADEON_PCIE_DATA:
                    val = s->regs.pcie_data;
                    break;
                    case RADEON_AIC_LO_ADDR:
                    val = s->regs.aic_lo_addr;
                    break;
                    case RADEON_AIC_HI_ADDR:
                    val = s->regs.aic_hi_addr;
                    break;
                    case RADEON_FP_GEN_CNTL:
                    val = s->regs.fp_gen_cntl;
                    break;
                    case RADEON_CRC_CMDFIFO_DOUT:

                    break;
                    case RADEON_DEVICE_ID:
                    val = PCI_DEVICE_ID_ATI_RADEON_9500_PRO;
                    break;
                    case RADEON_DAC_CNTL:
                    case RADEON_DAC_CNTL2:
                    case RADEON_DAC_MACRO_CNTL:
                    case RADEON_TV_DAC_CNTL:
                    case RADEON_DAC_CRC_SIG:
                    case RADEON_DAC_DATA:
                    case RADEON_DAC_MASK:
                    case RADEON_DAC_R_INDEX:
                    case RADEON_DAC_W_INDEX:
                    case RADEON_DAC_EXT_CNTL:
                    case RADEON_GPIOPAD_MASK:
                    case RADEON_GPIOPAD_A:
                    case RADEON_GPIOPAD_EN:
                    case RADEON_GPIOPAD_Y:
                    case RADEON_MDGPIO_MASK:
                    case RADEON_MDGPIO_A:
                    case RADEON_MDGPIO_EN:
                    case RADEON_MDGPIO_Y:
                    case RADEON_DISP_OUTPUT_CNTL:
                        val = vga_ioport_read(s,addr);
                    break;












    default:
        qemu_log("READING FROM 0x%08lx \n",addr);
        val = s->regs.emu_register_stub[addr % 1024];
        qemu_log("REGISTER 0x%08lx CONTAINS 0x%08lx \n",addr,val);
        break;
    }
    if (addr < RADEON_CUR_OFFSET || addr > RADEON_CUR_CLR1 || R300_DEBUG_HW_CURSOR) {
        trace_ati_mm_read(size, addr, r300_reg_name(addr & ~3ULL), val);
    }
    // qemu_log("R300 MM_READ ADDRESS 0x%08lx DATA 0x%08lx \n",addr,val);
    return val;
}

static inline void r300_reg_write_offs(uint32_t *reg, int offs,
                                      uint64_t data, unsigned int size)
{
    if (offs == 0 && size == 4) {
        *reg = data;
    } else {
        *reg = deposit32(*reg, offs * BITS_PER_BYTE, size * BITS_PER_BYTE,
                         data);
    }
}

static void r300_mm_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned int size)
{
    RADVGAState *s = opaque;

    // qemu_log("R300 MM_WRITE ADDRESS 0x%08lx DATA 0x%08lx \n",addr,data);

    if (addr < RADEON_CUR_OFFSET || addr > RADEON_CUR_CLR1 || R300_DEBUG_HW_CURSOR) {
        trace_ati_mm_write(size, addr, r300_reg_name(addr & ~3ULL), data);
    }
    switch (addr) {
      case RADEON_GENMO_WT:

            break;
      case RADEON_CP_CSQ_CNTL:
            s->regs.cp_csq_cntl = data;
            break;
      case RADEON_SCRATCH_UMSK:
            s->regs.scratch_umask = data;
            break;
      case R_00023C_DISPLAY_BASE_ADDR:
              s->regs.r100_display_base_addr = data;
              break;

      case RADEON_MC_STATUS:
        qemu_log("RADEON_WRITE_MC \n");
        s->regs.mc_status = R300_MC_IDLE;
        s->regs.mc_status = data;
        break;
      case RADEON_RBBM_STATUS:
      qemu_log("RADEON_WRITE_RBBM \n");
        s->regs.rbbm_status = data|= RADEON_RBBM_FIFOCNT_MASK;
        break;
    case RADEON_MM_INDEX:
        s->regs.mm_index = data;
        break;
    case RADEON_MM_DATA:
        s->regs.mm_data = data;

        break;
    case RADEON_BIOS_0_SCRATCH:
    {
      // qemu_log("RADEON_WRITE_BIOS_0_SCRATCH, %lx \n",data);
      s->regs.bios_scratch[0] = data;
        break;
    }
    case RADEON_BIOS_1_SCRATCH:
    {
      // qemu_log("RADEON_WRITE_BIOS_1_SCRATCH, %lx \n",data);
      s->regs.bios_scratch[1] = data;
        break;
    }
    case RADEON_BIOS_2_SCRATCH:
    {
      // qemu_log("RADEON_WRITE_BIOS_2_SCRATCH, %lx \n",data);
      s->regs.bios_scratch[2] = data;
        break;
    }
    case RADEON_BIOS_3_SCRATCH:
    {
      // qemu_log("RADEON_WRITE_BIOS_3_SCRATCH, %lx \n",data);
      s->regs.bios_scratch[3] = data;
        break;
    }
    case RADEON_BIOS_4_SCRATCH:
    {
      // qemu_log("RADEON_WRITE_BIOS_4_SCRATCH, %lx \n",data);
      s->regs.bios_scratch[4] = data;
        break;
    }
    case RADEON_BIOS_5_SCRATCH:
    {
      // qemu_log("RADEON_WRITE_BIOS_5_SCRATCH, %lx \n",data);
      s->regs.bios_scratch[5] = data;
        break;
    }
    case RADEON_BIOS_6_SCRATCH:
    {
      // qemu_log("RADEON_WRITE_BIOS_6_SCRATCH, %lx \n",data);
      s->regs.bios_scratch[6] = data;
        break;
    }
    case RADEON_BIOS_7_SCRATCH:
    {
      // qemu_log("RADEON_WRITE_BIOS_7_SCRATCH, %lx \n",data);
      s->regs.bios_scratch[7] = data;
        break;
    }
    case RADEON_GEN_INT_CNTL:
    // qemu_log("RADEON_WRITE_GEN_INT_CTL \n");
        s->regs.gen_int_cntl = data;

        break;
    case RADEON_GEN_INT_STATUS:
    // qemu_log("RADEON_WRITE_GEN_INT_STATUS \n");
        s->regs.gen_int_status =data;

        break;
    case RADEON_CRTC_GEN_CNTL:
    {
      // qemu_log("RADEON_WRITE_CRTC_GEN \n");
      s->regs.crtc_gen_cntl = data;
      vga_ioport_write(s,addr,data);
        break;
    }
    case RADEON_CRTC_EXT_CNTL:
    {
      vga_ioport_write(s,addr,data);
        break;
    }
    case RADEON_GPIO_VGA_DDC:
        s->regs.gpio_vga_ddc = data;
        break;
    case RADEON_GPIO_DVI_DDC:
            s->regs.gpio_dvi_ddc = data;

        break;
    case RADEON_GPIO_MONID:

        s->regs.gpio_monid = data;
        break;
    case RADEON_PALETTE_INDEX ... RADEON_PALETTE_INDEX + 3:

        break;
    case RADEON_PALETTE_DATA ... RADEON_PALETTE_DATA + 3:
        break;
    case RADEON_CONFIG_CNTL:
        s->regs.config_cntl = data;
        break;

    case RADEON_CUR_OFFSET:

            s->regs.cur_offset = data;
        break;

    case RADEON_DEFAULT_OFFSET:

            s->regs.default_offset = data;

        break;
    case RADEON_DEFAULT_PITCH:
        s->regs.default_pitch = data;


        break;
    case RADEON_DEFAULT_SC_BOTTOM_RIGHT:
        s->regs.default_sc_bottom_right = data;
        break;
    case R300_GB_ENABLE:
        s->regs.r300_gb_enable = data;
        break;
    case R300_GB_TILE_CONFIG:
            s->regs.r300_gb_tile_config = data;
            break;
    case R300_GB_FIFO_SIZE:
            s->regs.r300_gb_fifo_size = data;
            break;
    case RADEON_ISYNC_CNTL:
            s->regs.isync_cntl = data;
            break;
  case R300_DST_PIPE_CONFIG:
            s->regs.r300_dst_pipe_config = data;
            break;
  case R300_RB2D_DSTCACHE_MODE:
            s->regs.r300_rb2d_dstcache_mode = data;
            break;
  case RADEON_WAIT_UNTIL:
            s->regs.wait_until = data;
            break;
  case R300_GB_SELECT:
            s->regs.r300_gb_select = data;
            break;
  case R300_RB3D_DSTCACHE_CTLSTAT:
            s->regs.r300_rb3d_dstcache_ctlstat = data;
            break;
  case R300_RB3D_ZCACHE_CTLSTAT:
            s->regs.r300_rb3d_zcache_ctlstat = data;
            break;
  case R300_GB_AA_CONFIG:
            s->regs.r300_gb_aa_config = data;
            break;
  case R300_RE_SCISSORS_TL:
            s->regs.r300_re_scissors_tl = data;
            break;

  case R300_RE_SCISSORS_BR:
            s->regs.r300_re_scissors_br = data;
            break;
  case RADEON_HOST_PATH_CNTL:
            s->regs.host_path_cntl = data;
            break;

  case R300_GB_MSPOS0:
            s->regs.r300_gb_mpos_0 = data;
            break;

  case R300_GB_MSPOS1:
            s->regs.r300_gb_mpos_1 = data;
            break;
  case RADEON_SURFACE_CNTL:
            s->regs.surface_cntl = data;
            break;
  case RADEON_SURFACE0_INFO:
            s->regs.surface0_info = data;
            break;
  case RADEON_SURFACE1_INFO:
            s->regs.surface1_info = data;
            break;

  case RADEON_SURFACE2_INFO:
  s->regs.surface2_info = data;
  break;
  case RADEON_SURFACE3_INFO:
  s->regs.surface3_info = data;
  break;
  case RADEON_SURFACE4_INFO:
  s->regs.surface4_info = data;
  break;
  case RADEON_SURFACE5_INFO:
  s->regs.surface5_info = data;
  break;
  case RADEON_SURFACE6_INFO:
  s->regs.surface6_info = data;
  break;
  case RADEON_SURFACE7_INFO:
  s->regs.surface7_info = data;
  break;
  case RADEON_OV0_SCALE_CNTL:
  s->regs.ov0_scale_cntl = data;
  break;
  case RADEON_SUBPIC_CNTL:
  s->regs.subpic_cntl = data;
  break;
  case RADEON_VIPH_CONTROL:
  s->regs.viph_control = data;
  break;
  case RADEON_I2C_CNTL_1:
  s->regs.i2c_cntl_1 = data;
  break;
  case RADEON_DVI_I2C_CNTL_1:
  s->regs.dvi_i2c_cntl_1 = data;
  break;
  case RADEON_CAP0_TRIG_CNTL:
  s->regs.cap0_trig_cntl = data;
  break;
  case RADEON_CAP1_TRIG_CNTL:
  s->regs.cap1_trig_cntl = data;
  break;
  case RADEON_CUR2_OFFSET:
  s->regs.cur2_offset = data;
  break;
  case RADEON_CRTC2_GEN_CNTL:
  s->regs.crtc2_gen_cntl = data;
  break;
  // case RADEON_MEM_INTF_CNTL:
  // s->regs.mem_intf_cntl = data;
  // break;
  case RADEON_AGP_BASE_2:
  s->regs.agp_base_2 = data;
  break;
  case RADEON_AGP_BASE:
  s->regs.agp_base = data;
  break;
  case RADEON_MEM_ADDR_CONFIG:
  s->regs.mem_addr_config = data;
  break;
  case RADEON_DISPLAY2_BASE_ADDR:
  s->regs.display2_base_addr = data;
  break;
  case RADEON_SPLL_CNTL:
  s->regs.spll_cntl = data;
  break;
  case RADEON_VCLK_ECP_CNTL:
  s->regs.vclk_ecp_cntl = data;
  break;
  case RADEON_CP_RB_CNTL:
  s->regs.cp_rb_cntl = data;
  break;
  case RADEON_MEM_CNTL:
  s->regs.mem_cntl = data;
  break;
  case R300_CRTC_TILE_X0_Y0:
  s->regs.tile_x0_y0 = data;

  break;
  case R300_MC_INIT_MISC_LAT_TIMER:
  s->regs.r300_mc_init_misc_lat_timer = 0xDEADBEEF;
  break;
  case RADEON_AIC_CNTL:
  s->regs.aic_cntl = data;
  break;
  case RADEON_DDA_CONFIG:
  s->regs.dda_config = data;
  break;
  case RADEON_M_SPLL_REF_FB_DIV:
  s->regs.m_spll_ref_fb_div = data;
  break;
  case RADEON_SCLK_CNTL:
  s->regs.r100_sclk_cntl = data &
				    ~(RADEON_SCLK_FORCE_DISP2 |
				      RADEON_SCLK_FORCE_CP |
				      RADEON_SCLK_FORCE_HDP |
				      RADEON_SCLK_FORCE_DISP1 |
				      RADEON_SCLK_FORCE_TOP |
				      RADEON_SCLK_FORCE_E2 | R300_SCLK_FORCE_VAP
				      | RADEON_SCLK_FORCE_IDCT |
				      RADEON_SCLK_FORCE_VIP | R300_SCLK_FORCE_SR
				      | R300_SCLK_FORCE_PX | R300_SCLK_FORCE_TX
				      | R300_SCLK_FORCE_US |
				      RADEON_SCLK_FORCE_TV_SCLK |
				      R300_SCLK_FORCE_SU |
				      RADEON_SCLK_FORCE_OV0);
  break;
  case RADEON_PCI_GART_PAGE:
  qemu_log("WRITE GART \n");
  s->regs.pci_gart_page = data;
  qemu_log("REGISTER 0x%08lx CONTAINS 0x%08lx \n",addr,data);
  break;
  case RADEON_AIC_PT_BASE:
  qemu_log("R100 GART ADDR 0x%08lx GART PTR 0x%08lx \n",addr,data);
  s->regs.aic_pt_base = data;

  break;
  case RADEON_MC_AGP_LOCATION:
  s->regs.mc_agp_location = data;
  qemu_log("WRITE MC_AGP  ADDR 0x%08lx DATA 0x%08lx \n",addr,data);
  break;
  case RADEON_PCIE_INDEX:
  s->regs.pcie_index = data;
  break;
  case RADEON_PCIE_DATA:
  s->regs.pcie_data = data;
  break;
  case RADEON_AIC_LO_ADDR:
  s->regs.aic_lo_addr=data;
  break;
  case RADEON_AIC_HI_ADDR:
  s->regs.aic_hi_addr=data;
  break;
  case RADEON_FP_GEN_CNTL:
  s->regs.fp_gen_cntl = data;
  break;
  case RADEON_CRC_CMDFIFO_DOUT:

  break;
  case RADEON_DEVICE_ID:
  break;
  //MONITOR STUFF
  case RADEON_DAC_CNTL:
  case RADEON_DAC_CNTL2:
  case RADEON_DAC_MACRO_CNTL:
  case RADEON_TV_DAC_CNTL:
  case RADEON_DAC_CRC_SIG:
  case RADEON_DAC_DATA:
  case RADEON_DAC_MASK:
  case RADEON_DAC_R_INDEX:
  case RADEON_DAC_W_INDEX:
  case RADEON_DAC_EXT_CNTL:
  case RADEON_DISP_OUTPUT_CNTL:
    if(data > 0)
    qemu_log("DAC/DISPLAY ADDR %lx DATA %lx \n",addr,data);
    vga_ioport_write(s,addr,data);
  break;

  case RADEON_GPIOPAD_MASK:
  case RADEON_GPIOPAD_A:
  case RADEON_GPIOPAD_EN:
  case RADEON_GPIOPAD_Y:
  case RADEON_MDGPIO_MASK:
  case RADEON_MDGPIO_A:
  case RADEON_MDGPIO_EN:
  case RADEON_MDGPIO_Y:

  break;











    default:
    qemu_log("REGISTER NOT IMPLEMENTED 0x%08lx \n",addr);
    s->regs.emu_register_stub[addr % 1024] = data;
    // qemu_log("REGISTER NOT IMPLEMENTED INDEX 0x%08lx \n",addr % 1024);
    qemu_log("REGISTER NOT IMPLEMENTED DATA 0x%08lx \n",data);

        break;
    }

}
static void r300_gart_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size){
  qemu_log("GART_WRITE 0x%08lx \n",addr);
}
static uint64_t r300_gart_read(void *opaque, hwaddr addr, unsigned int size){
  qemu_log("GART_READ 0x%08lx \n",addr);
  return 0;
}

static const MemoryRegionOps r300_mm_ops = {
    .read = r300_mm_read,
    .write = r300_mm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};
static const MemoryRegionOps r300_gart_ops = {
  .read = r300_gart_read,
  .write = r300_gart_write,
  .endianness = DEVICE_LITTLE_ENDIAN,
};



static void r300_vga_realize(PCIDevice *dev, Error **errp)
{
    RADVGAState *s = RAD_VGA(dev);
    VGACommonState *vga = &s->vga;

    // s->regs.r300_mc_init_misc_lat_timer = 0xDEADBEEF;

    if (s->model) {
        s->regs.vga_reset = 0x02;
        s->regs.rbbm_status = 64;
        s->regs.r100_sclk_cntl =  ~(RADEON_SCLK_FORCE_DISP2 |
      				      RADEON_SCLK_FORCE_CP |
      				      RADEON_SCLK_FORCE_HDP |
      				      RADEON_SCLK_FORCE_DISP1 |
      				      RADEON_SCLK_FORCE_TOP |
      				      RADEON_SCLK_FORCE_E2 | R300_SCLK_FORCE_VAP
      				      | RADEON_SCLK_FORCE_IDCT |
      				      RADEON_SCLK_FORCE_VIP | R300_SCLK_FORCE_SR
      				      | R300_SCLK_FORCE_PX | R300_SCLK_FORCE_TX
      				      | R300_SCLK_FORCE_US |
      				      RADEON_SCLK_FORCE_TV_SCLK |
      				      R300_SCLK_FORCE_SU |
      				      RADEON_SCLK_FORCE_OV0);
        int i;
        for (i = 0; i < ARRAY_SIZE(r300_model_aliases); i++) {
            if (!strcmp(s->model, r300_model_aliases[i].name)) {
                s->dev_id = r300_model_aliases[i].dev_id;
                break;
            }
        }
        if (i >= ARRAY_SIZE(r300_model_aliases)) {
            warn_report("Unknown ATI VGA model name, "
                        "using default radeon9500");
        }
    }
    if (s->dev_id != PCI_DEVICE_ID_ATI_RADEON_9500_PRO) {
        error_setg(errp, "Unknown ATI VGA device id, "
                   "only 0x4e45 is supported");
        return;
    }
    pci_set_word(dev->config + PCI_DEVICE_ID, s->dev_id);

    if (s->dev_id == PCI_DEVICE_ID_ATI_RADEON_9500_PRO &&
        s->vga.vram_size_mb < 128) {
        warn_report("Too small video memory for device id");
        s->vga.vram_size_mb = 128;
    }

    /* init vga bits */
    vga_common_init(vga, OBJECT(s));
    vga_init(vga, OBJECT(s), pci_address_space(dev),
             pci_address_space_io(dev), true);
    vga->con = graphic_console_init(DEVICE(s), 0, s->vga.hw_ops, &s->vga);

    /* mmio register space */
    memory_region_init_io(&s->mm, OBJECT(s), &r300_mm_ops, s,"ati.mmregs", RADEON_MIN_MMIO_SIZE);
    /* io space is alias to beginning of mmregs */
    memory_region_init_alias(&s->io, OBJECT(s), "ati.io", &s->mm, 0, 0x100);
    /* GART address space */
    // memory_region_init_iommu(&s->gart,sizeof(&s->gart),TYPE_AMD_IOMMU_MEMORY_REGION,OBJECT(s),"ati.gart",1 * GiB);
    // address_space_init(&s->gart_as,MEMORY_REGION(&s->gart),"ati-dma");
    memory_region_init_io(&s->gart,OBJECT(s),&r300_gart_ops,s,"ati.gart",RADEON_MIN_MMIO_SIZE);
    // memory_region_init_alias(&s->gart,OBJECT(s),"ati.gart",&s->mm,RADEON_PCI_GART_PAGE,RADEON_MIN_MMIO_SIZE);


    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &vga->vram);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mm);
    pci_register_bar(dev,3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->gart);


    timer_init_ns(&s->vblank_timer, QEMU_CLOCK_VIRTUAL, r300_vga_vblank_irq, s);
}

static void r300_vga_reset(DeviceState *dev)
{
    RADVGAState *s = RAD_VGA(dev);
    s->regs.mc_status = R300_MC_IDLE;

    timer_del(&s->vblank_timer);
    r300_vga_update_irq(s);

    /* reset vga */
    vga_common_reset(&s->vga);
    s->mode = VGA_MODE;
}

static void r300_vga_exit(PCIDevice *dev)
{
    RADVGAState *s = RAD_VGA(dev);

    timer_del(&s->vblank_timer);
    graphic_console_close(s->vga.con);
}

static Property r300_vga_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", RADVGAState, vga.vram_size_mb, 1024),
    DEFINE_PROP_STRING("model", RADVGAState, model),
    DEFINE_PROP_UINT16("x-device-id", RADVGAState, dev_id,
                       PCI_DEVICE_ID_ATI_RADEON_9500_PRO),
    DEFINE_PROP_BOOL("guest_hwcursor", RADVGAState, cursor_guest_mode, false),
    DEFINE_PROP_END_OF_LIST()
};

static void r300_vga_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->reset = r300_vga_reset;
    dc->props = r300_vga_properties;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);

    k->class_id = PCI_CLASS_DISPLAY_VGA;
    k->vendor_id = PCI_VENDOR_ID_ATI;
    k->device_id = PCI_DEVICE_ID_ATI_RADEON_9500_PRO;
    k->romfile = "vgabios-ati.bin";
    k->realize = r300_vga_realize;
    k->exit = r300_vga_exit;
}

static const TypeInfo r300_vga_info = {
    .name = TYPE_RAD_VGA,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RADVGAState),
    .class_init = r300_vga_class_init,
    .interfaces = (InterfaceInfo[]) {
          { INTERFACE_CONVENTIONAL_PCI_DEVICE },
          { },
    },
};

static void r300_vga_register_types(void)
{
    type_register_static(&r300_vga_info);
}

type_init(r300_vga_register_types)
