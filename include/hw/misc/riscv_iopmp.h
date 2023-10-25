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

#define TYPE_IOPMP "iopmp"
#define IOPMP(obj) OBJECT_CHECK(IopmpState, (obj), TYPE_IOPMP)

#define iopmp_get_field(reg, name) (((reg) & (name ## _FIELD)) >> (name))
#define iopmp_set_field32(reg, name, newval) do { \
    uint32_t val = *reg; \
    val &= ~name##_FIELD; \
    val |= ((newval) << name) & name##_FIELD; \
    *reg = val; \
    } while (0)
#define iopmp_set_field64(reg, name, newval) do { \
    uint64_t val = *reg; \
    val &= ~name##_FIELD; \
    val |= ((newval) << name) & name##_FIELD; \
    *reg = val; \
    } while (0)


#define IOPMP_MAX_MD_NUM    63
#define IOPMP_MAX_SID_NUM   256
#define IOPMP_MAX_ENTRY_NUM 512

#define IOPMP_VERSION      0x0
#define IOPMP_IMP          0x4
#define IOPMP_HWCFG0       0x8
#define IOPMP_HWCFG1       0xC
#define IOPMP_HWCFG2       0x10
#define IOPMP_ENTRYOFFSET  0x20
#define IOPMP_ERRREACT     0x28
#define IOPMP_MDSTALL      0x30
#define IOPMP_MDSTALLH     0x34
#define IOPMP_SIDSCP       0x38
#define IOPMP_MDLCK        0x40
#define IOPMP_MDLCKH       0x44
#define IOPMP_MDCFGLCK     0x48
#define IOPMP_ENTRYLCK     0x4C

#define IOPMP_ERR_REQADDR  0x60
#define IOPMP_ERR_REQADDRH 0x64
#define IOPMP_ERR_REQSID   0x68
#define IOPMP_ERR_REQINFO  0x6C

#define IOPMP_MDCFG0       0x800
#define IOPMP_SRCMD_EN0    0x1000
#define IOPMP_SRCMD_ENH0   0x1004
#define IOPMP_SRCMD_R0     0x1008
#define IOPMP_SRCMD_RH0    0x100C
#define IOPMP_SRCMD_W0     0x1010
#define IOPMP_SRCMD_WH0    0x1014

#define IOPMP_ENTRY_ADDR0  0x4000
#define IOPMP_ENTRY_ADDRH0 0x4004
#define IOPMP_ENTRY_CFG0   0x4008
#define IOPMP_USER_CFG0    0x400C

#define VERSION_VENDOR       0
#define VERSION_SPECVER      24
#define VENDER_ANDES         6533
#define SPECVER_1_0_0_DRAFT4 4

#define IMPID_1_0_0_DRAFT4_0 10040

#define HWCFG0_SID_NUM     0
#define HWCFG0_ENTRY_NUM   16

#define HWCFG1_MODEL            0
#define HWCFG1_TOR_EN           4
#define HWCFG1_SPS_EN           5
#define HWCFG1_USER_CFG_EN      6
#define HWCFG1_PRIENT_PROG      7
#define HWCFG1_SID_TRANSL_EN    8
#define HWCFG1_SID_TRANSL_PROG  9
#define HWCFG1_MD_NUM           24
#define HWCFG1_ENABLE           31

#define HWCFG1_SPS_EN_FIELD          (1 << HWCFG1_SPS_EN)
#define HWCFG1_PRIENT_PROG_FIELD     (1 << HWCFG1_PRIENT_PROG)
#define HWCFG1_SID_TRANSL_PROG_FIELD (1 << HWCFG1_SID_TRANSL_PROG)
#define HWCFG1_ENABLE_FIELD          (1 << HWCFG1_ENABLE)

#define HWCFG2_PRIO_ENTRY       0
#define HWCFG2_SID_TRANSL      16

#define HWCFG2_PRIO_ENTRY_FIELD (0xFFFF << HWCFG2_PRIO_ENTRY)
#define HWCFG2_SID_TRANSL_FIELD (0xFFFF << HWCFG2_SID_TRANSL)

#define ERRREACT_L          0
#define ERRREACT_IE         1
#define ERRREACT_IP         2
#define ERRREACT_IRE        4
#define ERRREACT_RRE        5
#define ERRREACT_IWE        8
#define ERRREACT_RWE        9
#define ERRREACT_PEE        28
#define ERRREACT_RPE        29

#define ERRREACT_L_FIELD    (0x1 << ERRREACT_L)
#define ERRREACT_IE_FIELD   (0x1 << ERRREACT_IE)
#define ERRREACT_IP_FIELD   (0x1 << ERRREACT_IP)
#define ERRREACT_IRE_FIELD  (0x1 << ERRREACT_IRE)
#define ERRREACT_RRE_FIELD  (0x7 << ERRREACT_RRE)
#define ERRREACT_IWE_FIELD  (0x1 << ERRREACT_IWE)
#define ERRREACT_RWE_FIELD  (0x7 << ERRREACT_RWE)
#define ERRREACT_PEE_FIELD  (0x1 << ERRREACT_PEE)
#define ERRREACT_RPE_FIELD  (0x7 << ERRREACT_RPE)

#define RRE_BUS_ERROR       0
#define RRE_DECODE_ERROR    1
#define RRE_SUCCESS_ZEROS   2
#define RRE_SUCCESS_ONES    3

#define RWE_BUS_ERROR       0
#define RWE_DECODE_ERROR    1
#define RWE_SUCCESS         2

#define MDSTALL               0
#define MDSTALLH              32
#define MDSTALL_FIELD         UINT32_MAX
#define MDSTALLH_FIELD        (UINT64_MAX << MDSTALLH)
#define MDSTALL_EXEMPT        0
#define MDSTALL_EXEMPT_FIELD  (1 << MDSTALL_EXEMPT)
#define MDSTALL_ISSTALLED     0
#define MDSTALL_MD            1
#define MDSTALL_MD_FIELD      (0x7FFFFFFFFFFFFFFF << MDSTALL_MD)

#define SIDSCP_SID         0
#define SIDSCP_STAT        30
#define SIDSCP_OP          30
#define SIDSCP_SID_FIELD   (0xFFFF << SIDSCP_SID)
#define SIDSCP_STAT_FIELD  (0x3 << SIDSCP_STAT)
#define SIDSCP_OP_FIELD    (0x3 << SIDSCP_OP)
#define SIDSCP_OP_QUERY    0
#define SIDSCP_OP_STALL    1
#define SIDSCP_OP_NOTSTALL 2

#define MDLCK_L            0
#define MDLCK_MD           1

#define MDCFGLCK_L         0
#define MDCFGLCK_L_FIELD   (0x1 << MDCFGLCK_L)
#define MDCFGLCK_F         1
#define MDCFGLCK_F_FIELD   (0x7F << MDCFGLCK_F)

#define ENTRYLCK_L         0
#define ENTRYLCK_L_FIELD   (0x1 << MDCFGLCK_L)
#define ENTRYLCK_F         1
#define ENTRYLCK_F_FIELD   (0xFFFF << ENTRYLCK_F)

#define ERR_REQINFO_NO_HIT  0
#define ERR_REQINFO_PAR_HIT 1
#define ERR_REQINFO_TYPE    8
#define ERR_REQINFO_EID     16

#define ERR_REQINFO_NO_HIT_FIELD  (0x1 << ERR_REQINFO_NO_HIT)
#define ERR_REQINFO_PAR_HIT_FIELD (0x1 << ERR_REQINFO_PAR_HIT)
#define ERR_REQINFO_TYPE_FIELD    (0x3 << ERR_REQINFO_TYPE)
#define ERR_REQINFO_EID_FIELD     (0xFFFF << ERR_REQINFO_EID)

#define ERR_REQINFO_TYPE_READ  0
#define ERR_REQINFO_TYPE_WRITE 1
#define ERR_REQINFO_TYPE_USER  3

#define SRCMD_EN_L         0
#define SRCMD_EN_MD        1
#define SRCMD_EN_L_FIELD   (0x1 << SRCMD_EN_L)
#define SRCMD_EN_MD_FIELD  (0x7FFFFFFF << SRCMD_EN_MD)
#define SRCMD_ENH_MDH        32
#define SRCMD_ENH_MDH_FIELD (0xFFFFFFFFUL << SRCMD_ENH_MDH)

#define SRCMD_R_MD        1
#define SRCMD_R_MD_FIELD  (0x7FFFFFFF << SRCMD_R_MD)
#define SRCMD_RH_MDH        32
#define SRCMD_RH_MDH_FIELD (0xFFFFFFFFUL << SRCMD_RH_MDH)
#define SRCMD_W_MD        1
#define SRCMD_W_MD_FIELD  (0x7FFFFFFF << SRCMD_W_MD)
#define SRCMD_WH_MDH        32
#define SRCMD_WH_MDH_FIELD (0xFFFFFFFFUL << SRCMD_WH_MDH)

#define ENTRY_ADDR_ADDR         0
#define ENTRY_ADDR_ADDR_FIELD   0xFFFFFFFF
#define ENTRY_ADDRH_ADDRH       32
#define ENTRY_ADDRH_ADDRH_FIELD (0xFFFFFFFFUL << ENTRY_ADDRH_ADDRH)

#define ENTRY_CFG_R            0
#define ENTRY_CFG_W            1
#define ENTRY_CFG_X            2
#define ENTRY_CFG_A            3
#define ENTRY_CFG_A_FIELD      (0x3 << ENTRY_CFG_A)

#define IOPMP_MODEL_FULL       0
#define IOPMP_MODEL_RAPIDK     0x1
#define IOPMP_MODEL_DYNAMICK   0x2
#define IOPMP_MODEL_ISOLATION  0x3
#define IOPMP_MODEL_COMPACTK   0x4
#define IOPMP_MODEL_K          8

#define TOR_EN 1
#define SPS_EN 0
#define USER_CFG_EN   0
#define PROG_PRIENT   1
#define PRIO_ENTRY    IOPMP_MAX_ENTRY_NUM
#define SID_TRANSL_EN 0
#define SID_TRANSL    0

#define ENTRY_NO_HIT      0
#define ENTRY_PAR_HIT     1
#define ENTRY_HIT         2

typedef enum {
    IOPMP_READ      = 1 << 0,
    IOPMP_WRITE     = 1 << 1,
    IOPMP_EXEC      = 1 << 2,
    IOPMP_ADDRMODE  = 1 << 3,
} iopmp_priv_t;

typedef enum {
    IOPMP_AMATCH_OFF,  /* Null (off)                            */
    IOPMP_AMATCH_TOR,  /* Top of Range                          */
    IOPMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
    IOPMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} iopmp_am_t;

typedef struct {
    uint64_t addr_reg;
    uint32_t  cfg_reg;
} iopmp_entry_t;

typedef struct {
    target_ulong sa;
    target_ulong ea;
} iopmp_addr_t;

typedef struct {
    uint64_t srcmd_en[IOPMP_MAX_SID_NUM];
    uint64_t srcmd_r[IOPMP_MAX_SID_NUM];
    uint64_t srcmd_w[IOPMP_MAX_SID_NUM];
    uint32_t mdcfg[IOPMP_MAX_MD_NUM];
    iopmp_entry_t entry[IOPMP_MAX_ENTRY_NUM];
    uint64_t mdmsk;
    uint64_t mdlck;
    uint32_t entrylck;
    uint32_t mdcfglck;
    uint32_t arrlck;
    uint64_t mdstall;
    uint32_t sidscp;
    uint32_t errreact;
    uint64_t err_reqaddr;
    uint32_t err_reqsid;
    uint32_t err_reqinfo;
} iopmp_regs;

/* To verfiy the same transcation */
typedef struct iopmp_error_detail {
    uint32_t reqinfo;
    target_ulong addr_start;
    target_ulong addr_end;
} iopmp_error_detail;

typedef struct IopmpState {
    SysBusDevice parent_obj;
    iopmp_addr_t entry_addr[IOPMP_MAX_ENTRY_NUM];
    iopmp_error_detail prev_error[IOPMP_MAX_SID_NUM];
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

    AddressSpace iopmp_as;
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

DeviceState *iopmp_create(hwaddr addr, qemu_irq irq);
void cascade_iopmp(DeviceState *cur_dev, DeviceState *next_dev);

#endif
