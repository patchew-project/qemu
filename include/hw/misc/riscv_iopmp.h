/*
 * QEMU RISC-V IOPMP (Input Output Physical Memory Protection)
 *
 * Copyright (c) 2023 Andes Tech. Corp.
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
#include "hw/stream.h"
#include "hw/misc/riscv_iopmp_transaction_info.h"
#include "hw/pci/pci_bus.h"

#define TYPE_IOPMP "iopmp"
#define IOPMP(obj) OBJECT_CHECK(IopmpState, (obj), TYPE_IOPMP)

#define IOPMP_MAX_MD_NUM    63
#define IOPMP_MAX_SID_NUM   256
#define IOPMP_MAX_ENTRY_NUM 512

#define VENDER_VIRT          0
#define SPECVER_1_0_0_DRAFT4 4

#define IMPID_1_0_0_DRAFT4_0 10040

#define RRE_BUS_ERROR       0
#define RRE_DECODE_ERROR    1
#define RRE_SUCCESS_ZEROS   2
#define RRE_SUCCESS_ONES    3

#define RWE_BUS_ERROR       0
#define RWE_DECODE_ERROR    1
#define RWE_SUCCESS         2

#define SIDSCP_OP_QUERY    0
#define SIDSCP_OP_STALL    1
#define SIDSCP_OP_NOTSTALL 2

#define ERR_REQINFO_TYPE_READ  0
#define ERR_REQINFO_TYPE_WRITE 1
#define ERR_REQINFO_TYPE_USER  3

#define IOPMP_MODEL_FULL       0
#define IOPMP_MODEL_RAPIDK     0x1
#define IOPMP_MODEL_DYNAMICK   0x2
#define IOPMP_MODEL_ISOLATION  0x3
#define IOPMP_MODEL_COMPACTK   0x4
#define CFG_IOPMP_MODEL_K      8

#define CFG_TOR_EN 1
#define CFG_SPS_EN 0
#define CFG_USER_CFG_EN   0
#define CFG_PROG_PRIENT   1
#define CFG_PRIO_ENTRY    IOPMP_MAX_ENTRY_NUM
#define CFG_SID_TRANSL_EN 0
#define CFG_SID_TRANSL    0

#define ENTRY_NO_HIT      0
#define ENTRY_PAR_HIT     1
#define ENTRY_HIT         2

#define AXI_BURST_TYPE_FIX 0
#define AXI_BURST_TYPE_INC 1

typedef enum {
    IOPMP_AMATCH_OFF,  /* Null (off)                            */
    IOPMP_AMATCH_TOR,  /* Top of Range                          */
    IOPMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
    IOPMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} iopmp_am_t;

typedef struct {
    uint32_t addr_reg;
    uint32_t addrh_reg;
    uint32_t  cfg_reg;
} iopmp_entry_t;

typedef struct {
    uint64_t sa;
    uint64_t ea;
} iopmp_addr_t;

typedef struct {
    uint32_t srcmd_en[IOPMP_MAX_SID_NUM];
    uint32_t srcmd_enh[IOPMP_MAX_SID_NUM];
    uint32_t srcmd_r[IOPMP_MAX_SID_NUM];
    uint32_t srcmd_rh[IOPMP_MAX_SID_NUM];
    uint32_t srcmd_w[IOPMP_MAX_SID_NUM];
    uint32_t srcmd_wh[IOPMP_MAX_SID_NUM];
    uint32_t mdcfg[IOPMP_MAX_MD_NUM];
    iopmp_entry_t entry[IOPMP_MAX_ENTRY_NUM];
    uint32_t mdlck;
    uint32_t mdlckh;
    uint32_t entrylck;
    uint32_t mdcfglck;
    uint32_t arrlck;
    uint32_t mdstall;
    uint32_t mdstallh;
    uint32_t sidscp;
    uint32_t errreact;
    uint64_t err_reqaddr;
    uint32_t err_reqsid;
    uint32_t err_reqinfo;
} iopmp_regs;

/* To verfiy the same transcation */
typedef struct iopmp_transaction_state {
    bool supported;
    bool running;
    hwaddr start_addr;
    hwaddr end_addr;
} iopmp_transaction_state;

typedef struct iopmp_error_info {
    uint32_t reqinfo;
    hwaddr start_addr;
    hwaddr end_addr;
} iopmp_error_info;

typedef struct Iopmp_StreamSink {
    Object parent;
} Iopmp_StreamSink;

typedef struct iopmp_pci_as {
    void *iopmp;
    IOMMUMemoryRegion iommu;
    AddressSpace as;
} iopmp_pci_addressspcace;

typedef struct IopmpState {
    SysBusDevice parent_obj;
    iopmp_addr_t entry_addr[IOPMP_MAX_ENTRY_NUM];
    iopmp_transaction_state transaction_state[IOPMP_MAX_SID_NUM];
    QemuMutex iopmp_transaction_mutex;
    iopmp_error_info prev_error_info[IOPMP_MAX_SID_NUM];
    MemoryRegion mmio;
    IOMMUMemoryRegion iommu;
    IOMMUMemoryRegion *next_iommu;
    iopmp_regs regs;
    MemoryRegion *downstream;
    MemoryRegion blocked_io;
    MemoryRegion stall_io;
    char *model_str;
    uint32_t model;
    uint32_t k;
    bool sps_en;
    bool sid_transl_prog;
    bool prient_prog;
    bool sid_transl_en;
    uint32_t sid_transl;
    Iopmp_StreamSink transaction_info_sink;

    AddressSpace iopmp_sysbus_as;
    iopmp_pci_addressspcace *iopmp_pci[IOPMP_MAX_SID_NUM];
    AddressSpace downstream_as;
    AddressSpace blocked_io_as;
    AddressSpace stall_io_as;
    qemu_irq irq;
    bool enable;
    uint32_t sidscp_op[IOPMP_MAX_SID_NUM];
    uint64_t md_stall_stat;
    uint32_t prio_entry;

    uint32_t sid_num;
    uint32_t md_num;
    uint32_t entry_num;
} IopmpState;

void cascade_iopmp(DeviceState *cur_dev, DeviceState *next_dev);
void iopmp_setup_pci(DeviceState *iopmp_dev, PCIBus *bus);

#endif
