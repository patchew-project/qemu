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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"
#include "exec/exec-all.h"
#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/misc/riscv_iopmp.h"
#include "memory.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "trace.h"

#define TYPE_IOPMP_IOMMU_MEMORY_REGION "iopmp-iommu-memory-region"
#define TYPE_IOPMP_TRASACTION_INFO_SINK "iopmp_transaction_info_sink"

DECLARE_INSTANCE_CHECKER(Iopmp_StreamSink, IOPMP_TRASACTION_INFO_SINK,
                         TYPE_IOPMP_TRASACTION_INFO_SINK)

#define MEMTX_IOPMP_STALL (1 << 3)

REG32(VERSION, 0x00)
    FIELD(VERSION, VENDOR, 0, 24)
    FIELD(VERSION, SPECVER , 24, 8)
REG32(IMP, 0x04)
    FIELD(IMP, IMPID, 0, 32)
REG32(HWCFG0, 0x08)
    FIELD(HWCFG0, SID_NUM, 0, 16)
    FIELD(HWCFG0, ENTRY_NUM, 16, 16)
REG32(HWCFG1, 0x0C)
    FIELD(HWCFG1, MODEL, 0, 4)
    FIELD(HWCFG1, TOR_EN, 4, 1)
    FIELD(HWCFG1, SPS_EN, 5, 1)
    FIELD(HWCFG1, USER_CFG_EN, 6, 1)
    FIELD(HWCFG1, PRIENT_PROG, 7, 1)
    FIELD(HWCFG1, SID_TRANSL_EN, 8, 1)
    FIELD(HWCFG1, SID_TRANSL_PROG, 9, 1)
    FIELD(HWCFG1, MD_NUM, 24, 7)
    FIELD(HWCFG1, ENABLE, 31, 1)
REG32(HWCFG2, 0x10)
    FIELD(HWCFG2, PRIO_ENTRY, 0, 16)
    FIELD(HWCFG2, SID_TRANSL, 16, 16)
REG32(ENTRYOFFSET, 0x20)
    FIELD(ENTRYOFFSET, OFFSET, 0, 32)
REG32(ERRREACT, 0x28)
    FIELD(ERRREACT, L, 0, 1)
    FIELD(ERRREACT, IE, 1, 1)
    FIELD(ERRREACT, IP, 2, 1)
    FIELD(ERRREACT, IRE, 4, 1)
    FIELD(ERRREACT, RRE, 5, 3)
    FIELD(ERRREACT, IWE, 8, 1)
    FIELD(ERRREACT, RWE, 9, 3)
    FIELD(ERRREACT, PEE, 28, 1)
    FIELD(ERRREACT, RPE, 29, 3)
REG32(MDSTALL, 0x30)
    FIELD(MDSTALL, EXEMPT, 0, 1)
    FIELD(MDSTALL, MD, 1, 31)
REG32(MDSTALLH, 0x34)
    FIELD(MDSTALLH, MD, 0, 32)
REG32(SIDSCP, 0x38)
    FIELD(SIDSCP, SID, 0, 16)
    FIELD(SIDSCP, OP, 30, 2)
REG32(MDLCK, 0x40)
    FIELD(MDLCK, L, 0, 1)
    FIELD(MDLCK, MD, 1, 31)
REG32(MDLCKH, 0x44)
    FIELD(MDLCKH, MDH, 0, 32)
REG32(MDCFGLCK, 0x48)
    FIELD(MDCFGLCK, L, 0, 1)
    FIELD(MDCFGLCK, F, 1, 7)
REG32(ENTRYLCK, 0x4C)
    FIELD(ENTRYLCK, L, 0, 1)
    FIELD(ENTRYLCK, F, 1, 16)
REG32(ERR_REQADDR, 0x60)
    FIELD(ERR_REQADDR, ADDR, 0, 32)
REG32(ERR_REQADDRH, 0x64)
    FIELD(ERR_REQADDRH, ADDRH, 0, 32)
REG32(ERR_REQSID, 0x68)
    FIELD(ERR_REQSID, SID, 0, 32)
REG32(ERR_REQINFO, 0x6C)
    FIELD(ERR_REQINFO, NO_HIT, 0, 1)
    FIELD(ERR_REQINFO, PAR_HIT, 1, 1)
    FIELD(ERR_REQINFO, TYPE, 8, 3)
    FIELD(ERR_REQINFO, EID, 16, 16)
REG32(MDCFG0, 0x800)
    FIELD(MDCFG0, T, 0, 16)
REG32(SRCMD_EN0, 0x1000)
    FIELD(SRCMD_EN0, L, 0, 1)
    FIELD(SRCMD_EN0, MD, 1, 31)
REG32(SRCMD_ENH0, 0x1004)
    FIELD(SRCMD_ENH0, MDH, 0, 32)
REG32(SRCMD_R0, 0x1008)
    FIELD(SRCMD_R0, MD, 1, 31)
REG32(SRCMD_RH0, 0x100C)
    FIELD(SRCMD_RH0, MDH, 0, 32)
REG32(SRCMD_W0, 0x1010)
    FIELD(SRCMD_W0, MD, 1, 31)
REG32(SRCMD_WH0, 0x1014)
    FIELD(SRCMD_WH0, MDH, 0, 32)
REG32(ENTRY_ADDR0, 0x4000)
    FIELD(ENTRY_ADDR0, ADDR, 0, 32)
REG32(ENTRY_ADDRH0, 0x4004)
    FIELD(ENTRY_ADDRH0, ADDRH, 0, 32)
REG32(ENTRY_CFG0, 0x4008)
    FIELD(ENTRY_CFG0, R, 0, 1)
    FIELD(ENTRY_CFG0, W, 1, 1)
    FIELD(ENTRY_CFG0, X, 2, 1)
    FIELD(ENTRY_CFG0, A, 3, 2)
REG32(ENTRY_USER_CFG0, 0x400C)
    FIELD(ENTRY_USER_CFG0, IM, 0, 32)

/* Offsets to SRCMD_EN(i) */
#define SRCMD_EN_OFFSET  0x0
#define SRCMD_ENH_OFFSET 0x4
#define SRCMD_R_OFFSET   0x8
#define SRCMD_RH_OFFSET  0xC
#define SRCMD_W_OFFSET   0x10
#define SRCMD_WH_OFFSET  0x14

/* Offsets to ENTRY_ADDR(i) */
#define ENTRY_ADDR_OFFSET     0x0
#define ENTRY_ADDRH_OFFSET    0x4
#define ENTRY_CFG_OFFSET      0x8
#define ENTRY_USER_CFG_OFFSET 0xC

static void iopmp_decode_napot(uint64_t a, uint64_t *sa,
                               uint64_t *ea)
{
    /*
     * aaaa...aaa0   8-byte NAPOT range
     * aaaa...aa01   16-byte NAPOT range
     * aaaa...a011   32-byte NAPOT range
     * ...
     * aa01...1111   2^XLEN-byte NAPOT range
     * a011...1111   2^(XLEN+1)-byte NAPOT range
     * 0111...1111   2^(XLEN+2)-byte NAPOT range
     *  1111...1111   Reserved
     */

    a = (a << 2) | 0x3;
    *sa = a & (a + 1);
    *ea = a | (a + 1);
}

static void iopmp_update_rule(IopmpState *s, uint32_t entry_index)
{
    uint8_t this_cfg = s->regs.entry[entry_index].cfg_reg;
    uint64_t this_addr = s->regs.entry[entry_index].addr_reg |
                         ((uint64_t)s->regs.entry[entry_index].addrh_reg << 32);
    uint64_t prev_addr = 0u;
    uint64_t sa = 0u;
    uint64_t ea = 0u;

    if (entry_index >= 1u) {
        prev_addr = s->regs.entry[entry_index - 1].addr_reg |
                    ((uint64_t)s->regs.entry[entry_index - 1].addrh_reg << 32);
    }

    switch (FIELD_EX32(this_cfg, ENTRY_CFG0, A)) {
    case IOPMP_AMATCH_OFF:
        sa = 0u;
        ea = -1;
        break;

    case IOPMP_AMATCH_TOR:
        sa = (prev_addr) << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = ((this_addr) << 2) - 1u;
        if (sa > ea) {
            sa = ea = 0u;
        }
        break;

    case IOPMP_AMATCH_NA4:
        sa = this_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (sa + 4u) - 1u;
        break;

    case IOPMP_AMATCH_NAPOT:
        iopmp_decode_napot(this_addr, &sa, &ea);
        break;

    default:
        sa = 0u;
        ea = 0u;
        break;
    }

    s->entry_addr[entry_index].sa = sa;
    s->entry_addr[entry_index].ea = ea;
}

static uint64_t iopmp_read(void *opaque, hwaddr addr, unsigned size)
{
    IopmpState *s = IOPMP(opaque);
    uint32_t rz = 0;
    uint32_t sid, offset, idx;

    switch (addr) {
    case A_VERSION ... A_ENTRY_USER_CFG0 + 16 * (IOPMP_MAX_ENTRY_NUM - 1):
        switch (addr) {
        case A_VERSION:
            rz = VENDER_VIRT << R_VERSION_VENDOR_SHIFT |
                 SPECVER_1_0_0_DRAFT4 << R_VERSION_SPECVER_SHIFT;
            break;
        case A_IMP:
            rz = IMPID_1_0_0_DRAFT4_0;
            break;
        case A_HWCFG0:
            rz = s->sid_num << R_HWCFG0_SID_NUM_SHIFT |
                 s->entry_num << R_HWCFG0_ENTRY_NUM_SHIFT;
            break;
        case A_HWCFG1:
            rz = s->model << R_HWCFG1_MODEL_SHIFT |
                 CFG_TOR_EN << R_HWCFG1_TOR_EN_SHIFT |
                 s->sps_en << R_HWCFG1_SPS_EN_SHIFT |
                 CFG_USER_CFG_EN << R_HWCFG1_USER_CFG_EN_SHIFT  |
                 s->prient_prog << R_HWCFG1_PRIENT_PROG_SHIFT  |
                 s->sid_transl_en << R_HWCFG1_SID_TRANSL_EN_SHIFT  |
                 s->sid_transl_prog << R_HWCFG1_SID_TRANSL_PROG_SHIFT  |
                 s->md_num << R_HWCFG1_MD_NUM_SHIFT  |
                 s->enable << R_HWCFG1_ENABLE_SHIFT ;
            break;
        case A_HWCFG2:
            rz = s->prio_entry << R_HWCFG2_PRIO_ENTRY_SHIFT |
                 s->sid_transl << R_HWCFG2_SID_TRANSL_SHIFT;
            break;
        case A_ENTRYOFFSET:
            rz = A_ENTRY_ADDR0;
            break;
        case A_ERRREACT:
            rz = s->regs.errreact;
            break;
        case A_MDSTALL:
            rz = s->regs.mdstall;
            break;
        case A_MDSTALLH:
            rz = s->regs.mdstallh;
            break;
        case A_SIDSCP:
            sid = FIELD_EX32(s->regs.sidscp, SIDSCP, SID);
            if (sid < s->sid_num) {
                rz = sid | (s->sidscp_op[sid]) << R_SIDSCP_OP_SHIFT;
            } else {
                rz = sid | 3 << R_SIDSCP_OP_SHIFT;
            }
            break;
        case A_MDLCK:
            rz = s->regs.mdlck;
            break;
        case A_MDLCKH:
            rz = s->regs.mdlckh;
            break;
        case A_MDCFGLCK:
            rz = s->regs.mdcfglck;
            break;
        case A_ENTRYLCK:
            rz = s->regs.entrylck;
            break;
        case A_ERR_REQADDR:
            rz = s->regs.err_reqaddr & UINT32_MAX;
            break;
        case A_ERR_REQADDRH:
            rz = s->regs.err_reqaddr >> 32;
            break;
        case A_ERR_REQSID:
            rz = s->regs.err_reqsid;
            break;
        case A_ERR_REQINFO:
            rz = s->regs.err_reqinfo;
            break;

        default:
            if (addr >= A_MDCFG0 &&
                addr < A_MDCFG0 + 4 * (s->md_num - 1)) {
                offset = addr - A_MDCFG0;
                idx = offset >> 2;
                if (idx == 0 && offset == 0) {
                    rz = s->regs.mdcfg[idx];
                } else {
                    /* Only MDCFG0 is implemented in rapid-k model */
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                  __func__, (int)addr);
                }
            } else if (addr >= A_SRCMD_EN0 &&
                       addr < A_SRCMD_WH0 + 32 * (s->sid_num - 1)) {
                offset = addr - A_SRCMD_EN0;
                idx = offset >> 5;
                offset &= 0x1f;

                switch (offset) {
                case SRCMD_EN_OFFSET:
                    rz = s->regs.srcmd_en[idx];
                    break;
                case SRCMD_ENH_OFFSET:
                    rz = s->regs.srcmd_enh[idx];
                    break;
                case SRCMD_R_OFFSET:
                    rz = s->regs.srcmd_r[idx];
                    break;
                case SRCMD_RH_OFFSET:
                    rz = s->regs.srcmd_rh[idx];
                    break;
                case SRCMD_W_OFFSET:
                    rz = s->regs.srcmd_w[idx];
                    break;
                case SRCMD_WH_OFFSET:
                    rz = s->regs.srcmd_wh[idx];
                    break;
                default:
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                        __func__, (int)addr);
                }
            } else if (addr >= A_ENTRY_ADDR0 &&
                       addr < A_ENTRY_USER_CFG0 + 16 * (s->entry_num - 1)) {
                offset = addr - A_ENTRY_ADDR0;
                idx = offset >> 4;
                offset &= 0xf;

                switch (offset) {
                case ENTRY_ADDR_OFFSET:
                    rz = s->regs.entry[idx].addr_reg;
                    break;
                case ENTRY_ADDRH_OFFSET:
                    rz = s->regs.entry[idx].addrh_reg;
                    break;
                case ENTRY_CFG_OFFSET:
                    rz = s->regs.entry[idx].cfg_reg;
                    break;
                case ENTRY_USER_CFG_OFFSET:
                    /* Does not support user customized permission */
                    rz = 0;
                    break;
                default:
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                        __func__, (int)addr);
                    break;
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                              __func__, (int)addr);
            }
            break;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                      __func__, (int)addr);
    }
    trace_iopmp_read(addr, rz);
    return rz;
}

static void
iopmp_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IopmpState *s = IOPMP(opaque);
    int value_f;
    int reg_f;
    uint32_t sid, op, offset, idx;
    uint32_t value32 = value;
    trace_iopmp_write(addr, value32);

    switch (addr) {
    case A_VERSION ... A_ENTRY_USER_CFG0 + 16 * (IOPMP_MAX_ENTRY_NUM - 1):
        switch (addr) {
        case A_VERSION: /* RO */
            break;
        case A_IMP: /* RO */
            break;
        case A_HWCFG0: /* RO */
            break;
        case A_HWCFG1:
            if (FIELD_EX32(value32, HWCFG1, PRIENT_PROG)) {
                /* W1C */
                s->prient_prog = 0;
            }
            if (FIELD_EX32(value32, HWCFG1, SID_TRANSL_PROG)) {
                /* W1C */
                s->sid_transl_prog = 0;
            }
            if (FIELD_EX32(value32, HWCFG1, ENABLE)) {
                /* W1S */
                s->enable = 1;
            }
            break;
        case A_HWCFG2:
            if (s->prient_prog) {
                s->prio_entry = FIELD_EX32(value32, HWCFG2, PRIO_ENTRY);
            }
            if (s->sid_transl_en && s->sid_transl_prog) {
                s->sid_transl = FIELD_EX32(value32, HWCFG2, SID_TRANSL);
            }
            break;
        case A_ERRREACT:
            if (!FIELD_EX32(s->regs.errreact, ERRREACT, L)) {
                    s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, L,
                               FIELD_EX32(value32, ERRREACT, L));
                if (FIELD_EX32(value32, ERRREACT, IP)) {
                    s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT,
                                                  IP, 0);
                }
                s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, IE,
                                        FIELD_EX32(value32, ERRREACT, IE));
                s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, IRE,
                                        FIELD_EX32(value32, ERRREACT, IRE));
                s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, RRE,
                                        FIELD_EX32(value32, ERRREACT, RRE));
                s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, IWE,
                                        FIELD_EX32(value32, ERRREACT, IWE));
                s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, RWE,
                                        FIELD_EX32(value32, ERRREACT, RWE));
                s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, PEE,
                                        FIELD_EX32(value32, ERRREACT, PEE));
                s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, RPE,
                                        FIELD_EX32(value32, ERRREACT, RPE));
            } else {
                if (FIELD_EX32(value32, ERRREACT, IP)) {
                    s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT,
                                                  IP, 0);
                }
            }
            break;
        case A_MDSTALL:
            s->regs.mdstall = value32;
            break;
        case A_MDSTALLH:
            s->regs.mdstallh = value32;
            break;
        case A_SIDSCP:
            sid = FIELD_EX32(value32, SIDSCP, SID);
            op = FIELD_EX32(value32, SIDSCP, OP);
            if (sid < s->sid_num && op != SIDSCP_OP_QUERY) {
                s->sidscp_op[sid] = op;
                s->regs.sidscp = value32;
            } else {
                s->regs.sidscp = sid | (0x3 << R_SIDSCP_OP_SHIFT);
            }
            break;
        case A_MDLCK:
            if (!FIELD_EX32(s->regs.mdlck, MDLCK, L)) {
                s->regs.mdlck = value32;
            }
            break;
        case A_MDLCKH:
             if (!FIELD_EX32(s->regs.mdlck, MDLCK, L)) {
                s->regs.mdlckh = value32;
            }
            break;
        case A_MDCFGLCK:
            if (!FIELD_EX32(s->regs.mdcfglck, MDCFGLCK, L)) {
                value_f = FIELD_EX32(value32, MDCFGLCK, F);
                reg_f = FIELD_EX32(s->regs.mdcfglck, MDCFGLCK, F);
                if (value_f > reg_f) {
                    s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, F,
                                                  value_f);
                }
                s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, L,
                           FIELD_EX32(value32, MDCFGLCK, L));
            }
            break;
        case A_ENTRYLCK:
            if (!(FIELD_EX32(s->regs.entrylck, ENTRYLCK, L))) {
                value_f = FIELD_EX32(value32, ENTRYLCK, F);
                reg_f = FIELD_EX32(s->regs.entrylck, ENTRYLCK, F);
                if (value_f > reg_f) {
                    s->regs.entrylck = FIELD_DP32(s->regs.entrylck, ENTRYLCK, F,
                                                  value_f);
                }
                s->regs.entrylck = FIELD_DP32(s->regs.entrylck, ENTRYLCK, L,
                          FIELD_EX32(value32, ENTRYLCK, L));
            }
        case A_ERR_REQADDR: /* RO */
            break;
        case A_ERR_REQADDRH: /* RO */
            break;
        case A_ERR_REQSID: /* RO */
            break;
        case A_ERR_REQINFO: /* RO */
            break;

        default:
            if (addr >= A_MDCFG0 &&
                addr < A_MDCFG0 + 4 * (s->md_num - 1)) {
                offset = addr - A_MDCFG0;
                idx = offset >> 2;
                /* RO in rapid-k model */
                if (idx > 0) {
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                  __func__, (int)addr);
                }
            } else if (addr >= A_SRCMD_EN0 &&
                       addr < A_SRCMD_WH0 + 32 * (s->sid_num - 1)) {
                offset = addr - A_SRCMD_EN0;
                idx = offset >> 5;
                offset &= 0x1f;

                if (offset % 4) {
                    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                                  __func__, (int)addr);
                } else if (FIELD_EX32(s->regs.srcmd_en[idx], SRCMD_EN0, L)
                           == 0) {

                    switch (offset) {
                    case SRCMD_EN_OFFSET:
                        s->regs.srcmd_en[idx] =
                            FIELD_DP32(s->regs.srcmd_en[idx], SRCMD_EN0, L,
                                       FIELD_EX32(value32, SRCMD_EN0, L));

                        /* MD field is protected by mdlck */
                        value32 = (value32 & ~s->regs.mdlck) |
                                  (s->regs.srcmd_en[idx] & s->regs.mdlck);
                        s->regs.srcmd_en[idx] =
                            FIELD_DP32(s->regs.srcmd_en[idx], SRCMD_EN0, MD,
                                        FIELD_EX32(value32, SRCMD_EN0, MD));
                        break;
                    case SRCMD_ENH_OFFSET:
                        value32 = (value32 & ~s->regs.mdlckh) |
                                  (s->regs.srcmd_enh[idx] & s->regs.mdlckh);
                        s->regs.srcmd_enh[idx] =
                            FIELD_DP32(s->regs.srcmd_enh[idx], SRCMD_ENH0, MDH,
                                       value32);
                        break;
                    case SRCMD_R_OFFSET:
                        if (s->sps_en) {
                            value32 = (value32 & ~s->regs.mdlck) |
                                      (s->regs.srcmd_r[idx] & s->regs.mdlck);
                            s->regs.srcmd_r[idx] =
                                FIELD_DP32(s->regs.srcmd_r[idx], SRCMD_R0, MD,
                                    FIELD_EX32(value32, SRCMD_R0, MD));
                        }
                        break;
                    case SRCMD_RH_OFFSET:
                        if (s->sps_en) {
                            value32 = (value32 & ~s->regs.mdlckh) |
                                      (s->regs.srcmd_rh[idx] & s->regs.mdlckh);
                            s->regs.srcmd_rh[idx] =
                                FIELD_DP32(s->regs.srcmd_rh[idx], SRCMD_RH0,
                                           MDH, value32);
                        }
                        break;
                    case SRCMD_W_OFFSET:
                        if (s->sps_en) {
                            value32 = (value32 & ~s->regs.mdlck) |
                                      (s->regs.srcmd_w[idx] & s->regs.mdlck);
                            s->regs.srcmd_w[idx] =
                                FIELD_DP32(s->regs.srcmd_w[idx], SRCMD_W0, MD,
                                    FIELD_EX32(value32, SRCMD_W0, MD));
                        }
                        break;
                    case SRCMD_WH_OFFSET:
                        if (s->sps_en) {
                            value32 = (value32 & ~s->regs.mdlckh) |
                                      (s->regs.srcmd_wh[idx] & s->regs.mdlckh);
                            s->regs.srcmd_wh[idx] =
                                FIELD_DP32(s->regs.srcmd_wh[idx], SRCMD_WH0,
                                    MDH, value32);
                        }
                    default:
                        break;
                    }
                }
            } else if (addr >= A_ENTRY_ADDR0 &&
                       addr < A_ENTRY_USER_CFG0 + 16 * (s->entry_num - 1)) {
                offset = addr - A_ENTRY_ADDR0;
                idx = offset >> 4;
                offset &= 0xf;

                /* index < ENTRYLCK_F is protected */
                if (idx >= FIELD_EX32(s->regs.entrylck, ENTRYLCK, F)) {
                    switch (offset) {
                    case ENTRY_ADDR_OFFSET:
                        s->regs.entry[idx].addr_reg = value32;
                        break;
                    case ENTRY_ADDRH_OFFSET:
                        s->regs.entry[idx].addrh_reg = value32;
                        break;
                    case ENTRY_CFG_OFFSET:
                        s->regs.entry[idx].cfg_reg = value32;
                        break;
                    case ENTRY_USER_CFG_OFFSET:
                        /* Does not support user customized permission */
                        break;
                    default:
                        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n",
                            __func__, (int)addr);
                        break;
                    }
                    iopmp_update_rule(s, idx);
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n", __func__,
                              (int)addr);
            }
            /* If IOPMP permission of any addr has been changed, */
            /* flush TLB pages. */
            tlb_flush_all_cpus_synced(current_cpu);
            break;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad addr %x\n", __func__,
                      (int)addr);
    }
}

/* Match entry in memory domain */
static int match_entry_md(IopmpState *s, int md_idx, hwaddr s_addr,
                          hwaddr e_addr, int *entry_idx)
{
    int entry_idx_s, entry_idx_e;
    int result = ENTRY_NO_HIT;
    int i = 0;
    entry_idx_s = md_idx * s->regs.mdcfg[0];
    entry_idx_e = (md_idx + 1) * s->regs.mdcfg[0];

    if (entry_idx_s >= s->entry_num) {
        return result;
    }
    if (entry_idx_e > s->entry_num) {
        entry_idx_e = s->entry_num;
    }
    i = entry_idx_s;
    while (i < entry_idx_e) {
        if (s_addr >= s->entry_addr[i].sa && s_addr <= s->entry_addr[i].ea) {
            /* check end address */
            if (e_addr >= s->entry_addr[i].sa &&
                e_addr <= s->entry_addr[i].ea) {
                *entry_idx = i;
                return ENTRY_HIT;
            } else if (i >= s->prio_entry) {
                /* record result and continue for non-prio_entry */
                result = ENTRY_PAR_HIT;
                continue;
            } else {
                return ENTRY_PAR_HIT;
            }
        }
        i++;
    }
    return result;
}

static int match_entry(IopmpState *s, int sid, hwaddr s_addr, hwaddr e_addr,
                       int *match_md_idx, int *match_entry_idx)
{
    int cur_result = ENTRY_NO_HIT;
    int result = ENTRY_NO_HIT;
    /* Remove lock bit */
    uint64_t srcmd_en = ((uint64_t)s->regs.srcmd_en[sid] |
                         ((uint64_t)s->regs.srcmd_enh[sid] << 32)) >> 1;

    for (int md_idx = 0; md_idx < s->md_num; md_idx++) {
        if (srcmd_en & (1ULL << md_idx)) {
            cur_result = match_entry_md(s, md_idx, s_addr, e_addr,
                                        match_entry_idx);
            if (cur_result == ENTRY_HIT) {
                *match_md_idx = md_idx;
                return cur_result;
            }
            if (cur_result > result) {
                result = cur_result;
            }
        }
    }
    return result;
}

static bool check_md_stall(IopmpState *s, int md_idx)
{
    uint64_t mdstall = s->regs.mdstall | (uint64_t)s->regs.mdstallh << 32;
    uint64_t md_selected = mdstall & (1 << (md_idx + R_MDSTALL_EXEMPT_SHIFT));
    if (FIELD_EX32(s->regs.mdstall, MDSTALL, EXEMPT)) {
        return !md_selected;
    } else {
        return md_selected;
    }
}

static inline bool check_sidscp_stall(IopmpState *s, int sid)
{
    return s->sidscp_op[sid] == SIDSCP_OP_STALL;
}

static void iopmp_error_reaction(IopmpState *s, uint32_t id, hwaddr start,
                                 hwaddr end, uint32_t info)
{
    if (start == s->prev_error_info[id].start_addr &&
        end == s->prev_error_info[id].end_addr &&
        info == s->prev_error_info[id].reqinfo) {
            /* skip following error */
            return;
    }

    s->prev_error_info[id].start_addr = start;
    s->prev_error_info[id].end_addr = end;
    s->prev_error_info[id].reqinfo = info;

    if (!FIELD_EX32(s->regs.errreact, ERRREACT, IP)) {
        s->regs.errreact = FIELD_DP32(s->regs.errreact, ERRREACT, IP, 1);
        s->regs.err_reqsid = id;
        s->regs.err_reqaddr = start;
        s->regs.err_reqinfo = info;

        if (FIELD_EX32(info, ERR_REQINFO, TYPE) == ERR_REQINFO_TYPE_READ
            && FIELD_EX32(s->regs.errreact, ERRREACT, IE) &&
            FIELD_EX32(s->regs.errreact, ERRREACT, IRE)) {
            qemu_set_irq(s->irq, 1);
        }
        if (FIELD_EX32(info, ERR_REQINFO, TYPE) == ERR_REQINFO_TYPE_WRITE &&
            FIELD_EX32(s->regs.errreact, ERRREACT, IE) &&
            FIELD_EX32(s->regs.errreact, ERRREACT, IWE)) {
            qemu_set_irq(s->irq, 1);
        }
    }
}

static IOMMUTLBEntry iopmp_translate(IOMMUMemoryRegion *iommu, hwaddr addr,
                                     IOMMUAccessFlags flags, int iommu_idx)
{
    bool is_stalled = false;
    int pci_id = 0;
    int sid = iommu_idx;
    IopmpState *s;
    MemoryRegion *mr = MEMORY_REGION(iommu);
    hwaddr start_addr, end_addr;
    int entry_idx = -1;
    int md_idx = -1;
    int result, srcmd_rw;
    uint32_t error_info = 0;
    IOMMUTLBEntry entry = {
        .target_as = NULL,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = (~(hwaddr)0),
        .perm = IOMMU_NONE,
    };

    /* Find IOPMP of iommu */
    if (strncmp(mr->name, "riscv-iopmp-sysbus-iommu", 24) != 0) {
        sscanf(mr->name, "riscv-iopmp-pci-iommu%d", &pci_id);
        iopmp_pci_addressspcace *pci_s = container_of(iommu,
                                                      iopmp_pci_addressspcace,
                                                      iommu);
        s = IOPMP(pci_s->iopmp);
        entry.target_as = &s->downstream_as;
        /* If device does not specify sid, use id from pci */
        if (sid == 0) {
            sid = pci_id;
        }
    } else {
        s = IOPMP(container_of(iommu, IopmpState, iommu));
    }

    if (s->transaction_state[sid].supported) {
        /* get transaction_state if device supported */
        start_addr = s->transaction_state[sid].start_addr;
        end_addr = s->transaction_state[sid].end_addr;
        if (addr > end_addr || addr < start_addr ||
            !s->transaction_state[sid].running) {
            qemu_log_mask(LOG_GUEST_ERROR, "transaction_state error.");
        }
    } else {
        start_addr = addr;
        end_addr = addr;
    }

    if (!s->enable) {
        /* Bypass IOPMP */
        entry.perm = IOMMU_RW;
        return entry;
    }

    result = match_entry(s, sid, start_addr, end_addr, &md_idx, &entry_idx);
    if (result == ENTRY_HIT) {
        is_stalled = check_md_stall(s, md_idx) || check_sidscp_stall(s, sid);
        if (is_stalled) {
            entry.target_as = &s->stall_io_as;
            entry.perm = IOMMU_RW;
            return entry;
        }
        entry.perm = s->regs.entry[entry_idx].cfg_reg & 0x7;
        if (s->sps_en) {
            /* SPS extension does not affect x permission */
            if (md_idx <= 31) {
                srcmd_rw = 0x4 | ((s->regs.srcmd_r[sid] >>
                                  (md_idx + R_SRCMD_R0_MD_SHIFT)) & 0x1);
                srcmd_rw |= ((s->regs.srcmd_w[sid] >>
                             (md_idx + R_SRCMD_W0_MD_SHIFT)) & 0x1) << 1;
            } else {
                srcmd_rw = 0x4 | ((s->regs.srcmd_rh[sid] >>
                                  (md_idx + R_SRCMD_R0_MD_SHIFT - 32)) & 0x1);
                srcmd_rw |= ((s->regs.srcmd_wh[sid] >>
                             (md_idx + R_SRCMD_W0_MD_SHIFT - 32)) & 0x1) << 1;
            }
            entry.perm &= srcmd_rw;
        }
        if ((entry.perm & flags) == 0) {
            /* permission denied */
            error_info = FIELD_DP32(error_info, ERR_REQINFO, EID, entry_idx);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, TYPE, (flags - 1));
            iopmp_error_reaction(s, sid, start_addr, end_addr, error_info);
            entry.target_as = &s->blocked_io_as;
            entry.perm = IOMMU_RW;
        } else {
            entry.addr_mask = s->entry_addr[entry_idx].ea -
                              s->entry_addr[entry_idx].sa;
            /* clear error info */
            s->prev_error_info[sid].reqinfo = 0;
            if (s->sid_transl_en) {
                /* pass to next iopmp */
                if (s->next_iommu) {
                    int new_sid = s->sid_transl;
                    IopmpState *next_s = IOPMP(container_of(s->next_iommu,
                                                            IopmpState, iommu));
                    next_s->transaction_state[new_sid].supported = true;
                    while (next_s->transaction_state[new_sid].running) {
                        ;
                    }
                    /* Send transaction info to next IOPMP */
                    qemu_mutex_lock(&next_s->iopmp_transaction_mutex);
                    next_s->transaction_state[new_sid].running = 1;
                    qemu_mutex_unlock(&next_s->iopmp_transaction_mutex);
                    next_s->transaction_state[new_sid].start_addr = start_addr;
                    next_s->transaction_state[new_sid].end_addr = end_addr;
                    /* Get result from next IOPMP */
                    entry = iopmp_translate(s->next_iommu, addr, flags,
                                            s->sid_transl);

                    /* Finish the transaction */
                    qemu_mutex_lock(&next_s->iopmp_transaction_mutex);
                    next_s->transaction_state[new_sid].running = 0;
                    qemu_mutex_unlock(&next_s->iopmp_transaction_mutex);

                    return entry;
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR, "Next iopmp is not found.");
                }
            }
        }
    } else {
        if (result == ENTRY_PAR_HIT) {
            error_info = FIELD_DP32(error_info, ERR_REQINFO, PAR_HIT, 1);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, TYPE, (flags - 1));
            iopmp_error_reaction(s, sid, start_addr, end_addr, error_info);
        } else {
            error_info = FIELD_DP32(error_info, ERR_REQINFO, NO_HIT, 1);
            error_info = FIELD_DP32(error_info, ERR_REQINFO, TYPE, (flags - 1));
            iopmp_error_reaction(s, sid, start_addr, end_addr, error_info);
        }
        entry.target_as = &s->blocked_io_as;
        entry.perm = IOMMU_RW;
    }
    return entry;
}

static const MemoryRegionOps iopmp_ops = {
    .read = iopmp_read,
    .write = iopmp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 4, .max_access_size = 4}
};

static MemTxResult iopmp_block_write(void *opaque, hwaddr addr, uint64_t value,
                                     unsigned size, MemTxAttrs attrs)
{
    IopmpState *s = IOPMP(opaque);

    switch (FIELD_EX32(s->regs.errreact, ERRREACT, RWE)) {
    case RWE_BUS_ERROR:
        return MEMTX_ERROR;
        break;
    case RWE_DECODE_ERROR:
        return MEMTX_DECODE_ERROR;
        break;
    case RWE_SUCCESS:
        return MEMTX_OK;
        break;
    default:
        break;
    }
    return MEMTX_OK;
}

static MemTxResult iopmp_block_read(void *opaque, hwaddr addr, uint64_t *pdata,
                                    unsigned size, MemTxAttrs attrs)
{
    IopmpState *s = IOPMP(opaque);

    switch (FIELD_EX32(s->regs.errreact, ERRREACT, RRE)) {
    case RRE_BUS_ERROR:
        return MEMTX_ERROR;
        break;
    case RRE_DECODE_ERROR:
        return MEMTX_DECODE_ERROR;
        break;
    case RRE_SUCCESS_ZEROS:
        *pdata = 0;
        return MEMTX_OK;
        break;
    case RRE_SUCCESS_ONES:
        *pdata = UINT64_MAX;
        return MEMTX_OK;
        break;
    default:
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps iopmp_block_io_ops = {
    .read_with_attrs = iopmp_block_read,
    .write_with_attrs = iopmp_block_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static MemTxResult iopmp_handle_stall(IopmpState *s, hwaddr addr,
                                      MemTxAttrs attrs)
{
    return MEMTX_IOPMP_STALL;
}

static MemTxResult iopmp_stall_write(void *opaque, hwaddr addr, uint64_t value,
                                     unsigned size, MemTxAttrs attrs)
{
    IopmpState *s = IOPMP(opaque);

    return iopmp_handle_stall(s, addr, attrs);
}

static MemTxResult iopmp_stall_read(void *opaque, hwaddr addr, uint64_t *pdata,
                                    unsigned size, MemTxAttrs attrs)
{
    IopmpState *s = IOPMP(opaque);

    *pdata = 0;
    return iopmp_handle_stall(s, addr, attrs);
}

static const MemoryRegionOps iopmp_stall_io_ops = {
    .read_with_attrs = iopmp_stall_read,
    .write_with_attrs = iopmp_stall_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static void iopmp_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    IopmpState *s = IOPMP(dev);
    uint64_t size;

    s->downstream = get_system_memory();
    size = memory_region_size(s->downstream);
    qemu_mutex_init(&s->iopmp_transaction_mutex);
    s->prient_prog = 1;
    s->sid_num = MIN(s->sid_num, IOPMP_MAX_SID_NUM);
    s->md_num = MIN(s->md_num, IOPMP_MAX_MD_NUM);
    s->entry_num = MIN(s->entry_num, IOPMP_MAX_ENTRY_NUM);

    if (s->sid_transl_en) {
        s->sid_transl_prog = 1;
    }
    if (!s->model_str || strcmp(s->model_str, "rapidk") == 0) {
        /* apply default model */
        s->model = IOPMP_MODEL_RAPIDK;
        s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, F, s->md_num);
        s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, L, 1);
        s->regs.mdcfg[0] = s->k;
    } else {
        qemu_log_mask(LOG_UNIMP, "IOPMP model %s is not supported. "
                      "Vailid values is rapidk.", s->model_str);
    }
    memory_region_init_iommu(&s->iommu, sizeof(s->iommu),
                             TYPE_IOPMP_IOMMU_MEMORY_REGION,
                             obj, "riscv-iopmp-sysbus-iommu", UINT64_MAX);
    address_space_init(&s->iopmp_sysbus_as, MEMORY_REGION(&s->iommu), "iommu");
    memory_region_init_io(&s->mmio, obj, &iopmp_ops,
                          s, "iopmp-regs", 0x100000);
    sysbus_init_mmio(sbd, &s->mmio);
    memory_region_init_io(&s->blocked_io, obj, &iopmp_block_io_ops,
                          s, "iopmp-blocked-io", size);
    address_space_init(&s->downstream_as, s->downstream,
                       "iopmp-downstream-as");
    address_space_init(&s->blocked_io_as, &s->blocked_io,
                       "iopmp-blocked-io-as");

    memory_region_init_io(&s->stall_io, obj, &iopmp_stall_io_ops,
                          s, "iopmp-stall-io", size);
    address_space_init(&s->stall_io_as, &s->stall_io,
                       "iopmp-stall-io-as");

    object_initialize_child(OBJECT(s), "iopmp_transaction_info",
                            &s->transaction_info_sink,
                            TYPE_IOPMP_TRASACTION_INFO_SINK);
}

static void iopmp_reset(DeviceState *dev)
{
    IopmpState *s = IOPMP(dev);

    qemu_set_irq(s->irq, 0);
    memset(&s->regs, 0, sizeof(iopmp_regs));
    memset(&s->entry_addr, 0, IOPMP_MAX_ENTRY_NUM * sizeof(iopmp_addr_t));
    memset(&s->prev_error_info, 0,
           IOPMP_MAX_SID_NUM * sizeof(iopmp_error_info));
    s->regs.errreact = 0;
    s->prient_prog = 1;

    if (s->model == IOPMP_MODEL_RAPIDK) {
        s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, F, s->md_num);
        s->regs.mdcfglck = FIELD_DP32(s->regs.mdcfglck, MDCFGLCK, L, 1);
        s->regs.mdcfg[0] = s->k;
    }

    if (s->sid_transl_en) {
        s->sid_transl_prog = 1;
    }
}

static int iopmp_attrs_to_index(IOMMUMemoryRegion *iommu, MemTxAttrs attrs)
{
    return attrs.requester_id;
}

static void iopmp_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = iopmp_translate;
    imrc->attrs_to_index = iopmp_attrs_to_index;
}

static Property iopmp_property[] = {
    DEFINE_PROP_STRING("model", IopmpState, model_str),
    DEFINE_PROP_BOOL("sps_en", IopmpState, sps_en, false),
    DEFINE_PROP_BOOL("sid_transl_en", IopmpState, sid_transl_en, false),
    DEFINE_PROP_UINT32("k", IopmpState, k, CFG_IOPMP_MODEL_K),
    DEFINE_PROP_UINT32("prio_entry", IopmpState, prio_entry, CFG_PRIO_ENTRY),
    DEFINE_PROP_UINT32("sid_num", IopmpState, sid_num, IOPMP_MAX_SID_NUM),
    DEFINE_PROP_UINT32("md_num", IopmpState, md_num, IOPMP_MAX_MD_NUM),
    DEFINE_PROP_UINT32("entry_num", IopmpState, entry_num, IOPMP_MAX_ENTRY_NUM),
    DEFINE_PROP_END_OF_LIST(),
};

static void iopmp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, iopmp_property);
    dc->realize = iopmp_realize;
    dc->reset = iopmp_reset;
}

static void iopmp_init(Object *obj)
{
    IopmpState *s = IOPMP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
}

static const TypeInfo iopmp_info = {
    .name = TYPE_IOPMP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IopmpState),
    .instance_init = iopmp_init,
    .class_init = iopmp_class_init,
};

static const TypeInfo
iopmp_iommu_memory_region_info = {
    .name = TYPE_IOPMP_IOMMU_MEMORY_REGION,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = iopmp_iommu_memory_region_class_init,
};


void
cascade_iopmp(DeviceState *cur_dev, DeviceState *next_dev)
{
    IopmpState *s = IOPMP(cur_dev);
    IopmpState *next_s = IOPMP(next_dev);

    s->sid_transl_en = true;
    s->next_iommu = &next_s->iommu;
}

static AddressSpace *iopmp_find_add_as(PCIBus *bus, void *opaque, int devfn)
{
    IopmpState *s = opaque;
    uint32_t id = PCI_BUILD_BDF(pci_bus_num(bus), devfn) % s->sid_num;
    iopmp_pci_addressspcace *iopmp_pci = s->iopmp_pci[id];

    if (iopmp_pci == NULL) {
        g_autofree char *name;
        name = g_strdup_printf("riscv-iopmp-pci-iommu%d", id);
        iopmp_pci = g_new0(iopmp_pci_addressspcace, 1);
        iopmp_pci->iopmp = opaque;
        memory_region_init_iommu(&iopmp_pci->iommu,
                                 sizeof(iopmp_pci->iommu),
                                 TYPE_IOPMP_IOMMU_MEMORY_REGION,
                                 OBJECT(s), name, UINT64_MAX);
        address_space_init(&iopmp_pci->as,
                           MEMORY_REGION(&iopmp_pci->iommu), "iommu");
    }
    return &iopmp_pci->as;
}

static const PCIIOMMUOps iopmp_iommu_ops = {
    .get_address_space = iopmp_find_add_as,
};

void iopmp_setup_pci(DeviceState *iopmp_dev, PCIBus *bus)
{
    IopmpState *s = IOPMP(iopmp_dev);
    pci_setup_iommu(bus, &iopmp_iommu_ops, s);
}

static size_t
transaction_info_push(StreamSink *transaction_info_sink, unsigned char *buf,
                    size_t len, bool eop)
{
    Iopmp_StreamSink *ss = IOPMP_TRASACTION_INFO_SINK(transaction_info_sink);
    IopmpState *s = IOPMP(container_of(ss, IopmpState,
                                       transaction_info_sink));
    iopmp_transaction_info signal;
    uint32_t sid = signal.sid;

    memcpy(&signal, buf, len);

    if (s->transaction_state[sid].running) {
        if (eop) {
            /* Finish the transaction */
            qemu_mutex_lock(&s->iopmp_transaction_mutex);
            s->transaction_state[sid].running = 0;
            qemu_mutex_unlock(&s->iopmp_transaction_mutex);
            return 1;
        } else {
            /* Transaction is already running */
            return 0;
        }
    } else if (len == sizeof(iopmp_transaction_info)) {
        /* Get the transaction info */
        s->transaction_state[sid].supported = 1;
        qemu_mutex_lock(&s->iopmp_transaction_mutex);
        s->transaction_state[sid].running = 1;
        qemu_mutex_unlock(&s->iopmp_transaction_mutex);

        s->transaction_state[sid].start_addr = signal.start_addr;
        s->transaction_state[sid].end_addr = signal.end_addr;
        return 1;
    }
    return 0;
}

static void iopmp_transaction_info_sink_class_init(ObjectClass *klass,
                                                   void *data)
{
    StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);
    ssc->push = transaction_info_push;
}

static const TypeInfo transaction_info_sink = {
    .name = TYPE_IOPMP_TRASACTION_INFO_SINK,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(Iopmp_StreamSink),
    .class_init = iopmp_transaction_info_sink_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { }
    },
};

static void
iopmp_register_types(void)
{
    type_register_static(&iopmp_info);
    type_register_static(&iopmp_iommu_memory_region_info);
    type_register_static(&transaction_info_sink);
}

type_init(iopmp_register_types);
