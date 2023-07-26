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
