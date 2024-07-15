/*
 * QEMU RISC-V IOPMP (Input Output Physical Memory Protection)
 *
 * Copyright (c) 2023-2024 Andes Tech. Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_IOPMP_H
#define RISCV_IOPMP_H

#include "hw/sysbus.h"
#include "qemu/typedefs.h"
#include "memory.h"
#include "exec/hwaddr.h"

#define TYPE_IOPMP "iopmp"
#define IOPMP(obj) OBJECT_CHECK(IopmpState, (obj), TYPE_IOPMP)

#define IOPMP_MAX_MD_NUM            63
#define IOPMP_MAX_RRID_NUM          65535
#define IOPMP_MAX_ENTRY_NUM         65535

#define VENDER_VIRT                 0
#define SPECVER_0_9_1               91
#define IMPID_0_9_1                 91

#define RRE_ERROR                   0
#define RRE_SUCCESS_VALUE           1

#define RWE_ERROR                   0
#define RWE_SUCCESS                 1

#define RXE_ERROR                   0
#define RXE_SUCCESS_VALUE           1

#define ERR_REQINFO_TTYPE_READ      1
#define ERR_REQINFO_TTYPE_WRITE     2
#define ERR_REQINFO_TTYPE_FETCH     3
#define ERR_REQINFO_ETYPE_NOERROR   0
#define ERR_REQINFO_ETYPE_READ      1
#define ERR_REQINFO_ETYPE_WRITE     2
#define ERR_REQINFO_ETYPE_FETCH     3
#define ERR_REQINFO_ETYPE_PARHIT    4
#define ERR_REQINFO_ETYPE_NOHIT     5
#define ERR_REQINFO_ETYPE_RRID      6
#define ERR_REQINFO_ETYPE_USER      7

#define IOPMP_MODEL_FULL            0
#define IOPMP_MODEL_RAPIDK          0x1
#define IOPMP_MODEL_DYNAMICK        0x2
#define IOPMP_MODEL_ISOLATION       0x3
#define IOPMP_MODEL_COMPACTK        0x4

#define ENTRY_NO_HIT                0
#define ENTRY_PAR_HIT               1
#define ENTRY_HIT                   2

/* The generic iopmp address space which downstream is system memory */
extern AddressSpace iopmp_container_as;

typedef enum {
    IOPMP_AMATCH_OFF,  /* Null (off)                            */
    IOPMP_AMATCH_TOR,  /* Top of Range                          */
    IOPMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
    IOPMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} iopmp_am_t;

typedef enum {
    IOPMP_ACCESS_READ  = 1,
    IOPMP_ACCESS_WRITE = 2,
    IOPMP_ACCESS_FETCH = 3
} iopmp_access_type;

typedef enum {
    IOPMP_NONE = 0,
    IOPMP_RO   = 1,
    IOPMP_WO   = 2,
    IOPMP_RW   = 3,
    IOPMP_XO   = 4,
    IOPMP_RX   = 5,
    IOPMP_WX   = 6,
    IOPMP_RWX  = 7,
} iopmp_permission;

typedef struct {
    uint32_t addr_reg;
    uint32_t addrh_reg;
    uint32_t cfg_reg;
} iopmp_entry_t;

typedef struct {
    uint64_t sa;
    uint64_t ea;
} iopmp_addr_t;

typedef struct {
    uint32_t *srcmd_en;
    uint32_t *srcmd_enh;
    uint32_t *mdcfg;
    iopmp_entry_t *entry;
    uint32_t mdlck;
    uint32_t mdlckh;
    uint32_t entrylck;
    uint32_t mdcfglck;
    uint32_t mdstall;
    uint32_t mdstallh;
    uint32_t rridscp;
    uint32_t err_cfg;
    uint64_t err_reqaddr;
    uint32_t err_reqid;
    uint32_t err_reqinfo;
} iopmp_regs;


/* To detect partially hit */
typedef struct iopmp_transaction_state {
    bool running;
    bool supported;
    hwaddr start_addr;
    hwaddr end_addr;
} iopmp_transaction_state;

typedef struct IopmpState {
    SysBusDevice parent_obj;
    iopmp_addr_t *entry_addr;
    MemoryRegion mmio;
    IOMMUMemoryRegion iommu;
    IOMMUMemoryRegion *next_iommu;
    iopmp_regs regs;
    MemoryRegion *downstream;
    MemoryRegion blocked_r, blocked_w, blocked_x, blocked_rw, blocked_rx,
                 blocked_wx, blocked_rwx;
    MemoryRegion stall_io;
    uint32_t model;
    uint32_t k;
    bool prient_prog;
    bool default_prient_prog;
    iopmp_transaction_state *transaction_state;
    QemuMutex iopmp_transaction_mutex;

    AddressSpace downstream_as;
    AddressSpace blocked_r_as, blocked_w_as, blocked_x_as, blocked_rw_as,
                 blocked_rx_as, blocked_wx_as, blocked_rwx_as;
    qemu_irq irq;
    bool enable;

    uint32_t prio_entry;
    uint32_t rrid_num;
    uint32_t md_num;
    uint32_t entry_num;
    uint32_t entry_offset;
    uint32_t fabricated_v;
} IopmpState;

void iopmp_setup_system_memory(DeviceState *dev, const MemMapEntry *memmap,
                               uint32_t mapentry_num);

#endif
