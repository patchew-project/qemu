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

#define BCM2838_GENET_HFB_FILTER_REGS     offsetof(BCM2838GenetRegs, hfb)
#define BCM2838_GENET_HFB_FILTER_REG(reg) (BCM2838_GENET_HFB_FILTER_REGS \
                                           + offsetof(BCM2838GenetRegsHfb, reg))
#define BCM2838_GENET_HFB_FILTER_CNT      48
#define BCM2838_GENET_HFB_FILTER_SIZE     128

#define BCM2838_GENET_INTRL0_REG(reg)   (offsetof(BCM2838GenetRegs, intrl0) \
                                        + offsetof(BCM2838GenetRegsIntrl0, reg))
#define BCM2838_GENET_INTRL0_SET        BCM2838_GENET_INTRL0_REG(set)
#define BCM2838_GENET_INTRL0_CLEAR      BCM2838_GENET_INTRL0_REG(clear)
#define BCM2838_GENET_INTRL0_MASK_SET   BCM2838_GENET_INTRL0_REG(mask_set)
#define BCM2838_GENET_INTRL0_MASK_CLEAR BCM2838_GENET_INTRL0_REG(mask_clear)

#define BCM2838_GENET_INTRL1_REG(reg)   (offsetof(BCM2838GenetRegs, intrl1) \
                                        + offsetof(BCM2838GenetRegsIntrl1, reg))
#define BCM2838_GENET_INTRL1_SET        BCM2838_GENET_INTRL1_REG(set)
#define BCM2838_GENET_INTRL1_CLEAR      BCM2838_GENET_INTRL1_REG(clear)
#define BCM2838_GENET_INTRL1_MASK_SET   BCM2838_GENET_INTRL1_REG(mask_set)
#define BCM2838_GENET_INTRL1_MASK_CLEAR BCM2838_GENET_INTRL1_REG(mask_clear)

#define BCM2838_GENET_UMAC_REG(reg)     (offsetof(BCM2838GenetRegs, umac) \
                                         + offsetof(BCM2838GenetRegsUmac, reg))
#define BCM2838_GENET_UMAC_CMD          BCM2838_GENET_UMAC_REG(cmd)
#define BCM2838_GENET_UMAC_MAC0         BCM2838_GENET_UMAC_REG(mac0)
#define BCM2838_GENET_UMAC_MAC1         BCM2838_GENET_UMAC_REG(mac1)
#define BCM2838_GENET_UMAC_MDIO_CMD     BCM2838_GENET_UMAC_REG(mdio_cmd)

#define BCM2838_GENET_TDMA_REGS         offsetof(BCM2838GenetRegs, tdma)
#define BCM2838_GENET_TDMA_REG(reg)     (BCM2838_GENET_TDMA_REGS \
                                         + offsetof(BCM2838GenetRegsTdma, reg))
#define BCM2838_GENET_TDMA_RINGS        BCM2838_GENET_TDMA_REG(rings)
#define BCM2838_GENET_TDMA_RING_CFG     BCM2838_GENET_TDMA_REG(ring_cfg)
#define BCM2838_GENET_TDMA_CTRL         BCM2838_GENET_TDMA_REG(ctrl)

#define BCM2838_GENET_RDMA_REGS         offsetof(BCM2838GenetRegs, rdma)
#define BCM2838_GENET_RDMA_REG(reg)     (BCM2838_GENET_RDMA_REGS \
                                         + offsetof(BCM2838GenetRegsRdma, reg))
#define BCM2838_GENET_RDMA_RINGS        BCM2838_GENET_RDMA_REG(rings)
#define BCM2838_GENET_RDMA_RING_CFG     BCM2838_GENET_RDMA_REG(ring_cfg)
#define BCM2838_GENET_RDMA_CTRL         BCM2838_GENET_RDMA_REG(ctrl)

#define BCM2838_GENET_TRING_REG(reg)    offsetof(BCM2838GenetTdmaRing, reg)
#define BCM2838_GENET_TRING_WRITE_PTR BCM2838_GENET_TRING_REG(write_ptr)
#define BCM2838_GENET_TRING_WRITE_PTR_HI BCM2838_GENET_TRING_REG(write_ptr_hi)
#define BCM2838_GENET_TRING_PROD_INDEX BCM2838_GENET_TRING_REG(prod_index)
#define BCM2838_GENET_TRING_CONS_INDEX BCM2838_GENET_TRING_REG(cons_index)
#define BCM2838_GENET_TRING_RING_BUF_SIZE BCM2838_GENET_TRING_REG(ring_buf_size)
#define BCM2838_GENET_TRING_RING_START_ADDR BCM2838_GENET_TRING_REG(start_addr)
#define BCM2838_GENET_TRING_RING_START_ADDR_HI BCM2838_GENET_TRING_REG(start_addr_hi)
#define BCM2838_GENET_TRING_RING_END_ADDR BCM2838_GENET_TRING_REG(end_addr)
#define BCM2838_GENET_TRING_RING_END_ADDR_HI BCM2838_GENET_TRING_REG(end_addr_hi)
#define BCM2838_GENET_TRING_RING_MBUF_DONE_TRESH BCM2838_GENET_TRING_REG(mbuf_done_tresh)
#define BCM2838_GENET_TRING_RING_FLOW_PERIOD BCM2838_GENET_TRING_REG(flow_period)
#define BCM2838_GENET_TRING_RING_READ_PTR BCM2838_GENET_TRING_REG(read_ptr)
#define BCM2838_GENET_TRING_RING_READ_PTR_HI BCM2838_GENET_TRING_REG(read_ptr_hi)

#define BCM2838_GENET_RRING_REG(reg)    offsetof(BCM2838GenetRdmaRing, reg)
#define BCM2838_GENET_RRING_WRITE_PTR BCM2838_GENET_RRING_REG(write_ptr)
#define BCM2838_GENET_RRING_WRITE_PTR_HI BCM2838_GENET_RRING_REG(write_ptr_hi)
#define BCM2838_GENET_RRING_PROD_INDEX BCM2838_GENET_RRING_REG(prod_index)
#define BCM2838_GENET_RRING_CONS_INDEX BCM2838_GENET_RRING_REG(cons_index)
#define BCM2838_GENET_RRING_RING_BUF_SIZE BCM2838_GENET_RRING_REG(ring_buf_size)
#define BCM2838_GENET_RRING_RING_START_ADDR BCM2838_GENET_RRING_REG(start_addr)
#define BCM2838_GENET_RRING_RING_START_ADDR_HI BCM2838_GENET_RRING_REG(start_addr_hi)
#define BCM2838_GENET_RRING_RING_END_ADDR BCM2838_GENET_RRING_REG(end_addr)
#define BCM2838_GENET_RRING_RING_END_ADDR_HI BCM2838_GENET_RRING_REG(end_addr_hi)
#define BCM2838_GENET_RRING_RING_MBUF_DONE_TRESH BCM2838_GENET_RRING_REG(mbuf_done_tresh)
#define BCM2838_GENET_RRING_RING_XON_XOFF_TRESH BCM2838_GENET_RRING_REG(xon_xoff_tresh)
#define BCM2838_GENET_RRING_RING_READ_PTR BCM2838_GENET_RRING_REG(read_ptr)
#define BCM2838_GENET_RRING_RING_READ_PTR_HI BCM2838_GENET_RRING_REG(read_ptr_hi)


#define BCM2838_GENET_PHY_REG(reg)      (offsetof(BCM2838GenetPhyRegs, reg) / 2)
#define BCM2838_GENET_PHY_BMCR          BCM2838_GENET_PHY_REG(bmcr)
#define BCM2838_GENET_PHY_AUX_CTL       BCM2838_GENET_PHY_REG(aux_ctl)
#define BCM2838_GENET_PHY_SHD           BCM2838_GENET_PHY_REG(shd)
#define BCM2838_GENET_EXP_DATA          BCM2838_GENET_PHY_REG(exp_data)
#define BCM2838_GENET_EXP_SEL           BCM2838_GENET_PHY_REG(exp_ctrl)

#define BCM2838_GENET_PHY_AUX_CTL_MISC  0x7
#define BCM2838_GENET_PHY_AUX_CTL_REGS_SIZE 8

#define SIZEOF_FIELD(type, field)      sizeof(((type*) 0)->field)
#define BCM2838_GENET_PHY_EXP_SHD_BLOCKS_CNT \
    (1u << (8 * SIZEOF_FIELD(BCM2838GenetPhyExpSel, block_id)))
#define BCM2838_GENET_PHY_EXP_SHD_REGS_CNT \
    (1u << (8 * SIZEOF_FIELD(BCM2838GenetPhyExpSel, reg_id)))

#define MAX_FRAME_SIZE                  0xFFF
#define MAX_PACKET_SIZE                 1518
#define MAX_PAYLOAD_SIZE                1500
#define TX_MIN_PKT_SIZE                 60

typedef union BCM2838GenetTxCsumInfo {
    uint32_t value;
    struct {
        uint32_t offset:15;
        uint32_t proto_udp:1;
        uint32_t start:15;
        uint32_t lv:1;
    };
} BCM2838GenetTxCsumInfo;

typedef struct QEMU_PACKED BCM2838GenetXmitStatus {
    uint32_t                length_status;  /* length and peripheral status */
    uint32_t                ext_status;     /* Extended status */
    uint32_t                rx_csum;        /* partial rx checksum */
    uint32_t                unused1[9];     /* unused */
    BCM2838GenetTxCsumInfo  tx_csum_info;   /* Tx checksum info. */
    uint32_t                unused2[3];     /* unused */
} BCM2838GenetXmitStatus;

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
        uint32_t reserved_0_3:4;
        uint32_t do_csum:1;
        uint32_t ow_crc:1;
        uint32_t append_crc:1;
        uint32_t reserved_7_8:2;
        uint32_t underrun:1;
        uint32_t reserved_10_11:2;
        uint32_t wrap:1;
        uint32_t sop:1;
        uint32_t eop:1;
        uint32_t own:1;
        uint32_t buflength:12;
        uint32_t reserved_28_31:4;
    } fields;
} BCM2838GenetTdmaLengthStatus;

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
    BCM2838GenetTdmaLengthStatus length_status;
    uint32_t address_lo;
    uint32_t address_hi;
} __attribute__((__packed__)) BCM2838GenetTdmaDesc;

typedef struct {
    uint32_t read_ptr;
    uint32_t read_ptr_hi;
    BCM2838GenetDmaConsIndex cons_index;
    BCM2838GenetDmaProdIndex prod_index;
    uint32_t ring_buf_size;
    uint32_t start_addr;
    uint32_t start_addr_hi;
    uint32_t end_addr;
    uint32_t end_addr_hi;
    uint32_t mbuf_done_tresh;
    uint32_t flow_period;
    uint32_t write_ptr;
    uint32_t write_ptr_hi;
    uint8_t reserved_0x34[0xC];
} __attribute__((__packed__)) BCM2838GenetTdmaRing;

typedef struct {
    BCM2838GenetTdmaDesc descs[BCM2838_GENET_DMA_DESC_CNT];
    BCM2838GenetTdmaRing rings[BCM2838_GENET_DMA_RING_CNT];
    BCM2838GenetDmaRingCfg ring_cfg;
    BCM2838GenetDmaCtrl ctrl;
    BCM2838GenetDmaStatus status;
    uint32_t scb_burst_size;
    uint8_t reserved_0x1050[0x1C];
    uint32_t arb_ctrl;
    uint32_t priority[3];
    uint8_t reserved_0x10D0[0xF84];
} __attribute__((__packed__)) BCM2838GenetRegsTdma;

typedef struct {
    uint8_t flt[BCM2838_GENET_HFB_FILTER_CNT * BCM2838_GENET_HFB_FILTER_SIZE
                * sizeof(uint32_t)];
    uint8_t reserved_0x6000[0x1C00];
    uint32_t ctrl;
    uint32_t flt_enable[2];
    uint8_t reserved_0x7C0C[0x10];
    uint32_t flt_len[BCM2838_GENET_HFB_FILTER_CNT / sizeof(uint32_t)];
    uint8_t reserved_0x7C4C[0x3B4];
} __attribute__((__packed__)) BCM2838GenetRegsHfb;

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
    BCM2838GenetRegsTdma tdma;
    uint8_t reserved_0x6000[0x2000];
    BCM2838GenetRegsHfb hfb;
} __attribute__((__packed__)) BCM2838GenetRegs;

typedef union {
    uint16_t value;
    struct {
        uint16_t reserved_0_5:6;
        uint16_t speed1000:1;
        uint16_t ctst:1;
        uint16_t fulldplx:1;
        uint16_t anrestart:1;
        uint16_t isolate:1;
        uint16_t pdown:1;
        uint16_t aenable:1;
        uint16_t speed100:1;
        uint16_t loopback:1;
        uint16_t reset:1;
    } fields;
} BCM2838GenetPhyBmcr;

typedef union {
    uint16_t value;
    struct {
        uint16_t ercap:1;
        uint16_t jcd:1;
        uint16_t lstatus:1;
        uint16_t anegcapable:1;
        uint16_t rfault:1;
        uint16_t anegcomplete:1;
        uint16_t reserved_6_7:2;
        uint16_t estaten:1;
        uint16_t _100half2:1;
        uint16_t _100full2:1;
        uint16_t _10half:1;
        uint16_t _10full:1;
        uint16_t _100half:1;
        uint16_t _100full:1;
        uint16_t _100base4:1;
    } fields;
} BCM2838GenetPhyBmsr;

typedef union {
    uint16_t value;
    struct {
        uint16_t slct:5;
        uint16_t _10half_1000xfull:1;
        uint16_t _10full_1000xhalf:1;
        uint16_t _100half_1000xpause:1;
        uint16_t _100full_1000xpause_asym:1;
        uint16_t _100base4:1;
        uint16_t pause_cap:1;
        uint16_t pause_asym:1;
        uint16_t reserved_12:1;
        uint16_t rfault:1;
        uint16_t lpack:1;
        uint16_t npage:1;
    } fields;
} BCM2838GenetPhyLpa;

typedef union {
    uint16_t value;
    struct {
        uint16_t reserved_0_9:10;
        uint16_t _1000half:1;
        uint16_t _1000full:1;
        uint16_t _1000remrxok:1;
        uint16_t _1000localrxok:1;
        uint16_t _1000msres:1;
        uint16_t _1000msfail:1;
    } fields;
} BCM2838GenetPhyStat1000;

typedef union {
    uint16_t value;
    struct {
        uint16_t reg_id_mask:3;
        uint16_t reserved_3:1;
        uint16_t reg_data:8;
        uint16_t reg_id:3;
        uint16_t misc_wren:1;
    } fields_1;
    struct {
        uint16_t reserved_0_3:4;
        uint16_t reg_data:12;
    } fields_2;
} BCM2838GenetPhyAuxCtl;

typedef union {
    uint16_t value;
    struct {
        uint16_t reg_data:10;
        uint16_t reg_id:5;
        uint16_t wr:1;
    } fields;
} BCM2838GenetPhyShadow;


typedef struct {
    uint8_t reg_id;
    uint8_t block_id;
}  __attribute__((__packed__)) BCM2838GenetPhyExpSel;

typedef struct {
    BCM2838GenetPhyBmcr bmcr;
    BCM2838GenetPhyBmsr bmsr;
    uint16_t sid1;
    uint16_t sid2;
    uint16_t advertise;
    BCM2838GenetPhyLpa lpa;
    uint16_t expansion;
    uint16_t next_page;
    uint16_t lpa_next_page;
    uint16_t ctrl1000;
    BCM2838GenetPhyStat1000 stat1000;
    uint16_t reserved_11_12[2];
    uint16_t mmd_ctrl;
    uint16_t mmd_data;
    uint16_t estatus;
    uint16_t ecr;
    uint16_t esr;
    uint16_t dcounter;
    uint16_t fcscounter;
    uint16_t nwaytest;
    uint16_t exp_data;
    uint16_t srevision;
    BCM2838GenetPhyExpSel exp_ctrl;
    BCM2838GenetPhyAuxCtl aux_ctl;
    uint16_t phyaddr;
    uint16_t isr;
    uint16_t imr;
    BCM2838GenetPhyShadow shd;
    uint16_t reserved_29;
    uint16_t rdb_addr;
    uint16_t rdb_data;
} __attribute__((__packed__)) BCM2838GenetPhyRegs;

typedef struct {
    uint16_t reserved_0_2[3];
    uint16_t clk_ctl;
    uint16_t scr2;
    uint16_t scr3;
    uint16_t reserved_6_9[4];
    uint16_t apd;
    uint16_t rgmii_mode;
    uint16_t reserved_12;
    uint16_t leds1;
    uint16_t reserved_14_18[5];
    uint16_t _100fx_ctrl;
    uint16_t ssd;
    uint16_t reserved_21_30[10];
    uint16_t mode;
} __attribute__((__packed__)) BCM2838GenetPhyShdRegs;

typedef struct {
    uint16_t auxctl;
    uint16_t reserved_1_6[BCM2838_GENET_PHY_AUX_CTL_REGS_SIZE - 2];
    uint16_t misc;
} __attribute__((__packed__)) BCM2838GenetPhyAuxShdRegs;

typedef struct {
    uint16_t regs[BCM2838_GENET_PHY_EXP_SHD_BLOCKS_CNT]
                 [BCM2838_GENET_PHY_EXP_SHD_REGS_CNT];
} __attribute__((__packed__)) BCM2838GenetPhyExpShdRegs;

struct BCM2838GenetState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    NICState *nic;
    NICConf nic_conf;

    MemoryRegion regs_mr;
    AddressSpace dma_as;

    BCM2838GenetRegs regs;
    BCM2838GenetPhyRegs phy_regs;
    BCM2838GenetPhyShdRegs phy_shd_regs;
    BCM2838GenetPhyAuxShdRegs phy_aux_ctl_shd_regs;
    BCM2838GenetPhyExpShdRegs phy_exp_shd_regs;

    qemu_irq irq_default;
    qemu_irq irq_prio;

    uint8_t tx_packet[MAX_FRAME_SIZE];
};

#endif /* BCM2838_GENET_H */
