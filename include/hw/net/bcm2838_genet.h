/*
 * BCM2838 Gigabit Ethernet emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_GENET_H
#define BCM2838_GENET_H

#include "net/net.h"
#include "hw/sysbus.h"

#define TYPE_BCM2838_GENET "bcm2838-genet"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2838GenetState, BCM2838_GENET)

#define BCM2838_GENET_REV_MAJOR         6
#define BCM2838_GENET_REV_MINOR         0

#define BCM2838_GENET_DMA_DESC_CNT      256
#define BCM2838_GENET_DMA_RING_CNT      17
#define BCM2838_GENET_DMA_RING_DEFAULT  (BCM2838_GENET_DMA_RING_CNT - 1)

typedef union {
    uint32_t value;
    struct {
        uint32_t gphy_rev:16;
        uint32_t minor_rev:4;
        uint32_t reserved_20_23:4;
        uint32_t major_rev:4;
        uint32_t reserved_28_31:4;
    } fields;
} BCM2838GenetSysRevCtrl;

typedef union {
    uint32_t value;
    struct {
        uint32_t scb:1;
        uint32_t ephy:1;
        uint32_t phy_det_r:1;
        uint32_t phy_det_f:1;
        uint32_t link_up:1;
        uint32_t link_down:1;
        uint32_t umac:1;
        uint32_t umac_tsv:1;
        uint32_t tbuf_underrun:1;
        uint32_t rbuf_overflow:1;
        uint32_t hfb_sm:1;
        uint32_t hfb_mm:1;
        uint32_t mpd_r:1;
        uint32_t rxdma_mbdone:1;
        uint32_t rxdma_pdone:1;
        uint32_t rxdma_bdone:1;
        uint32_t txdma_mbdone:1;
        uint32_t txdma_pdone:1;
        uint32_t txdma_bdone:1;
        uint32_t reserved_19_22:4;
        uint32_t mdio_done:1;
        uint32_t mdio_error:1;
        uint32_t reserved_25_31:7;
    } fields;
} BCM2838GenetIntrl0;

typedef union {
    uint32_t value;
    struct {
        uint32_t tx_intrs:16;
        uint32_t rx_intrs:16;
    } fields;
} BCM2838GenetIntrl1;

typedef union {
    uint32_t value;
    struct {
        uint32_t tx_en:1;
        uint32_t rx_en:1;
        uint32_t speed:2;
        uint32_t promisc:1;
        uint32_t pad_en:1;
        uint32_t crc_fwd:1;
        uint32_t pause_fwd:1;
        uint32_t rx_pause_ignore:1;
        uint32_t tx_addr_ins:1;
        uint32_t hd_en:1;
        uint32_t sw_reset_old:1;
        uint32_t reserved_12:1;
        uint32_t sw_reset:1;
        uint32_t reserved_14:1;
        uint32_t lcl_loop_en:1;
        uint32_t reserved_16_21:6;
        uint32_t auto_config:1;
        uint32_t cntl_frm_en:1;
        uint32_t no_len_chk:1;
        uint32_t rmt_loop_en:1;
        uint32_t rx_err_disc:1;
        uint32_t prbl_en:1;
        uint32_t tx_pause_ignore:1;
        uint32_t tx_rx_en:1;
        uint32_t runt_filter_dis:1;
        uint32_t reserved_31:1;
    } fields;
} BCM2838GenetUmacCmd;

typedef union {
    uint32_t value;
    struct {
        uint32_t addr_3:8;
        uint32_t addr_2:8;
        uint32_t addr_1:8;
        uint32_t addr_0:8;
    } fields;
} BCM2838GenetUmacMac0;

typedef union {
    uint32_t value;
    struct {
        uint32_t addr_5:8;
        uint32_t addr_4:8;
        uint32_t reserved_16_31:16;
    } fields;
} BCM2838GenetUmacMac1;

typedef union {
    uint32_t value;
    struct {
        uint32_t reg_data:16;
        uint32_t reg_id:5;
        uint32_t phy_id:5;
        uint32_t wr:1;
        uint32_t rd:1;
        uint32_t rd_fail:1;
        uint32_t start_busy:1;
        uint32_t reserved_30_31:2;
    } fields;
} BCM2838GenetUmacMdioCmd;

typedef union {
    uint32_t value;
    struct {
        uint32_t en:17;
        uint32_t reserved_17_31:15;
    } fields;
} BCM2838GenetDmaRingCfg;

typedef union {
    uint32_t value;
    struct {
        uint32_t en:1;
        uint32_t ring_buf_en:17;
        uint32_t reserved_18_19:2;
        uint32_t tsb_swap_en:1;
        uint32_t reserved_21_31:11;
    } fields;
} BCM2838GenetDmaCtrl;

typedef union {
    uint32_t value;
    struct {
        uint32_t index:16;
        uint32_t discard_cnt:16;
    } fields;
} BCM2838GenetDmaProdIndex;

typedef union {
    uint32_t value;
    struct {
        uint32_t index:16;
        uint32_t reserved_16_31:16;
    } fields;
} BCM2838GenetDmaConsIndex;

typedef union {
    uint32_t value;
    struct {
        uint32_t disabled:1;
        uint32_t desc_ram_init_busy:1;
        uint32_t reserved_2_31:30;
    } fields;
} BCM2838GenetDmaStatus;

typedef union {
    uint32_t value;
    struct {
        uint32_t overrun:1;
        uint32_t crc_error:1;
        uint32_t rxerr:1;
        uint32_t no:1;
        uint32_t lg:1;
        uint32_t multicast:1;
        uint32_t broadcast:1;
        uint32_t reserved_7_11:5;
        uint32_t wrap:1;
        uint32_t sop:1;
        uint32_t eop:1;
        uint32_t own:1;
        uint32_t buflength:12;
        uint32_t reserved_28_31:4;
    } fields;
} BCM2838GenetRdmaLengthStatus;

typedef struct {
    BCM2838GenetSysRevCtrl rev_ctrl;
    uint32_t port_ctrl;
    uint32_t rbuf_flush_ctrl;
    uint32_t tbuf_flush_ctrl;
    uint8_t reserved_0x10[0x30];
} __attribute__((__packed__)) BCM2838GenetRegsSys;

typedef struct {
    uint8_t reserved_0x0[0x40];
} __attribute__((__packed__)) BCM2838GenetRegsGrBridge;

typedef struct {
    uint32_t pwr_mgmt;
    uint8_t reserved_0x4[0x8];
    uint32_t rgmii_oob_ctrl;
    uint8_t reserved_0x10[0xC];
    uint32_t gphy_ctrl;
    uint8_t reserved_0x20[0x60];
} __attribute__((__packed__)) BCM2838GenetRegsExt;

typedef struct {
    BCM2838GenetIntrl0 stat;
    BCM2838GenetIntrl0 set;
    BCM2838GenetIntrl0 clear;
    BCM2838GenetIntrl0 mask_status;
    BCM2838GenetIntrl0 mask_set;
    BCM2838GenetIntrl0 mask_clear;
    uint8_t reserved_0x18[0x28];
} __attribute__((__packed__)) BCM2838GenetRegsIntrl0;

typedef struct {
    BCM2838GenetIntrl1 stat;
    BCM2838GenetIntrl1 set;
    BCM2838GenetIntrl1 clear;
    BCM2838GenetIntrl1 mask_status;
    BCM2838GenetIntrl1 mask_set;
    BCM2838GenetIntrl1 mask_clear;
    uint8_t reserved_0x18[0x28];
} __attribute__((__packed__)) BCM2838GenetRegsIntrl1;

typedef struct {
    uint32_t ctrl;
    uint8_t reserved_0x4[0x8];
    uint32_t status;
    uint8_t reserved_0x10[0x4];
    uint32_t chk_ctrl;
    uint8_t reserved_0x18[0x7C];
    uint32_t ovfl_cnt;
    uint32_t err_cnt;
    uint32_t energy_ctrl;
    uint8_t reserved_0xA0[0x14];
    uint32_t size_ctrl;
    uint8_t reserved_0xB8[0x48];
} __attribute__((__packed__)) BCM2838GenetRegsRbuf;

typedef struct {
    uint32_t ctrl;
    uint8_t reserved_0x4[0x8];
    uint32_t bp_mc;
    uint8_t reserved_0x10[0x4];
    uint32_t energy_ctrl;
    uint8_t reserved_0x18[0xE8];
} __attribute__((__packed__)) BCM2838GenetRegsTbuf;

typedef struct {
    uint8_t reserved_0x0[0x4];
    uint32_t hd_bkp_ctrl;
    BCM2838GenetUmacCmd cmd;
    BCM2838GenetUmacMac0 mac0;
    BCM2838GenetUmacMac1 mac1;
    uint32_t max_frame_len;
    uint32_t pause_quanta;
    uint8_t reserved_0x1C[0x28];
    uint32_t mode;
    uint32_t frm_tag0;
    uint32_t frm_tag1;
    uint8_t reserved_0x50[0xC];
    uint32_t tx_ipg_len;
    uint8_t reserved_0x60[0x4];
    uint32_t eee_ctrl;
    uint32_t eee_lpi_timer;
    uint32_t eee_wake_timer;
    uint32_t eee_ref_count;
    uint8_t reserved_0x74[0x4];
    uint32_t rx_ipg_inv;
    uint8_t reserved_0x7C[0x294];
    uint32_t macsec_prog_tx_crc;
    uint32_t macsec_ctrl;
    uint8_t reserved_0x318[0x18];
    uint32_t pause_ctrl;
    uint32_t tx_flush;
    uint32_t rx_fifo_status;
    uint32_t tx_fifo_status;
    uint8_t reserved_0x340[0xC0];
    uint8_t mib[0x180];
    uint32_t mib_ctrl;
    uint8_t reserved_0x584[0x90];
    BCM2838GenetUmacMdioCmd mdio_cmd;
    uint8_t reserved_0x618[0x8];
    uint32_t mpd_ctrl;
    uint32_t mpd_pw_ms;
    uint32_t mpd_pw_ls;
    uint8_t reserved_0x62C[0xC];
    uint32_t mdf_err_cnt;
    uint8_t reserved_0x63C[0x14];
    uint32_t mdf_ctrl;
    uint32_t mdf_addr;
    uint8_t reserved_0x658[0x1A8];
} __attribute__((__packed__)) BCM2838GenetRegsUmac;

typedef struct {
    BCM2838GenetRdmaLengthStatus length_status;
    uint32_t address_lo;
    uint32_t address_hi;
} __attribute__((__packed__)) BCM2838GenetRdmaDesc;

typedef struct {
    uint32_t write_ptr;
    uint32_t write_ptr_hi;
    BCM2838GenetDmaProdIndex prod_index;
    BCM2838GenetDmaConsIndex cons_index;
    uint32_t ring_buf_size;
    uint32_t start_addr;
    uint32_t start_addr_hi;
    uint32_t end_addr;
    uint32_t end_addr_hi;
    uint32_t mbuf_done_tresh;
    uint32_t xon_xoff_tresh;
    uint32_t read_ptr;
    uint32_t read_ptr_hi;
    uint8_t reserved_0x34[0xC];
} __attribute__((__packed__)) BCM2838GenetRdmaRing;

typedef struct {
    BCM2838GenetRdmaDesc descs[BCM2838_GENET_DMA_DESC_CNT];
    BCM2838GenetRdmaRing rings[BCM2838_GENET_DMA_RING_CNT];
    BCM2838GenetDmaRingCfg ring_cfg;
    BCM2838GenetDmaCtrl ctrl;
    BCM2838GenetDmaStatus status;
    uint32_t scb_burst_size;
    uint8_t reserved_0x1050[0x1C];
    uint32_t ring_timeout[17];
    uint32_t index2ring[8];
    uint8_t reserved_0x10D0[0xF30];
} __attribute__((__packed__)) BCM2838GenetRegsRdma;

typedef struct {
    BCM2838GenetRegsSys sys;
    BCM2838GenetRegsGrBridge gr_bridge;
    BCM2838GenetRegsExt ext;
    uint8_t reserved_0x100[0x100];
    BCM2838GenetRegsIntrl0 intrl0;
    BCM2838GenetRegsIntrl1 intrl1;
    uint8_t reserved_0x280[0x80];
    BCM2838GenetRegsRbuf rbuf;
    uint8_t reserved_0x400[0x200];
    BCM2838GenetRegsTbuf tbuf;
    uint8_t reserved_0x700[0x100];
    BCM2838GenetRegsUmac umac;
    uint8_t reserved_0x1000[0x1000];
    BCM2838GenetRegsRdma rdma;
} __attribute__((__packed__)) BCM2838GenetRegs;

struct BCM2838GenetState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    MemoryRegion regs_mr;
    AddressSpace dma_as;

    BCM2838GenetRegs regs;

    qemu_irq irq_default;
    qemu_irq irq_prio;
};

#endif /* BCM2838_GENET_H */
