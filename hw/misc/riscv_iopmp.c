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

#define TYPE_IOPMP_IOMMU_MEMORY_REGION "iopmp-iommu-memory-region"

#define LOGGE(x...) qemu_log_mask(LOG_GUEST_ERROR, x)
#define xLOG(x...)
#define yLOG(x...) qemu_log(x)
#ifdef DEBUG_RISCV_IOPMP
  #define LOG(x...) yLOG(x)
#else
  #define LOG(x...) xLOG(x)
#endif

#define MEMTX_IOPMP_STALL (1 << 3)


static void iopmp_decode_napot(target_ulong a, target_ulong *sa,
                               target_ulong *ea)
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
    target_ulong this_addr = s->regs.entry[entry_index].addr_reg;
    target_ulong prev_addr = 0u;
    target_ulong sa = 0u;
    target_ulong ea = 0u;

    if (entry_index >= 1u) {
        prev_addr = s->regs.entry[entry_index - 1].addr_reg;
    }

    switch (iopmp_get_field(this_cfg, ENTRY_CFG_A)) {
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

static uint64_t
iopmp_read(void *opaque, hwaddr addr, unsigned size)
{
    IopmpState *s = IOPMP(opaque);
    uint32_t rz = 0;
    uint32_t is_stall = 0;
    uint32_t sid;
    switch (addr) {
    case IOPMP_VERSION ... IOPMP_USER_CFG0 + 16 * (IOPMP_MAX_ENTRY_NUM - 1):
        switch (addr) {
        case IOPMP_VERSION:
            rz = VENDER_ANDES << VERSION_VENDOR |
                 SPECVER_1_0_0_DRAFT4 << VERSION_SPECVER;
            break;
        case IOPMP_IMP:
            rz = IMPID_1_0_0_DRAFT4_0;
            break;
        case IOPMP_HWCFG0: /* RO */
            rz = s->sid_num << HWCFG0_SID_NUM |
                 s->entry_num << HWCFG0_ENTRY_NUM;
            break;
        case IOPMP_HWCFG1:
            rz = s->model << HWCFG1_MODEL | TOR_EN << HWCFG1_TOR_EN |
                 s->sps_en << HWCFG1_SPS_EN |
                 USER_CFG_EN << HWCFG1_USER_CFG_EN |
                 s->prient_prog << HWCFG1_PRIENT_PROG |
                 s->sid_transl_en << HWCFG1_SID_TRANSL_EN |
                 s->sid_transl_prog << HWCFG1_SID_TRANSL_PROG |
                 s->md_num << HWCFG1_MD_NUM |
                 s->enable << HWCFG1_ENABLE;
            break;
        case IOPMP_HWCFG2:
            rz = s->prio_entry << HWCFG2_PRIO_ENTRY |
                 s->sid_transl << HWCFG2_SID_TRANSL;
            break;
        case IOPMP_ENTRYOFFSET:
            rz = IOPMP_ENTRY_ADDR0;
            break;
        case IOPMP_ERRREACT:
            rz = s->regs.errreact;
            break;
        case IOPMP_MDSTALL:
            if (s->md_stall_stat) {
                is_stall = 1;
            }
            rz = iopmp_get_field(s->regs.mdstall, MDSTALL_MD) | is_stall;
            break;
        case IOPMP_MDSTALLH:
            rz = s->regs.mdstall >> 32;
            break;
        case IOPMP_SIDSCP:
            sid = iopmp_get_field(s->regs.sidscp, SIDSCP_SID);
            if (sid < s->sid_num) {
                rz = sid | (s->sidscp_op[sid]) << SIDSCP_STAT;
            } else {
                rz = sid | 3 << SIDSCP_STAT;
            }
            break;
        case IOPMP_MDLCK:
            rz = s->regs.mdlck & UINT32_MAX;
            break;
        case IOPMP_MDLCKH:
            rz = s->regs.mdlck >> 32;
            break;
        case IOPMP_MDCFGLCK:
            rz = s->regs.mdcfglck;
            break;
        case IOPMP_ENTRYLCK:
            rz = s->regs.entrylck;
            break;
        case IOPMP_ERR_REQADDR:
            rz = s->regs.err_reqaddr & UINT32_MAX;
            break;
        case IOPMP_ERR_REQADDRH:
            rz = s->regs.err_reqaddr >> 32;
            break;
        case IOPMP_ERR_REQSID:
            rz = s->regs.err_reqsid;
            break;
        case IOPMP_ERR_REQINFO:
            rz = s->regs.err_reqinfo;
            break;

        default:
            if (addr >= IOPMP_MDCFG0 &&
                addr < IOPMP_MDCFG0 + 4 * (s->md_num - 1)) {
                int offset = addr - IOPMP_MDCFG0;
                int idx = offset >> 2;
                if (idx == 0) {
                    if (offset == 0) {
                        rz = s->regs.mdcfg[idx];
                    } else {
                        LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
                    }
                } else {
                    /* Only MDCFG0 is implemented */
                    LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
                }
            } else if (addr >= IOPMP_SRCMD_EN0 &&
                       addr < IOPMP_SRCMD_WH0 + 32 * (s->sid_num - 1)) {
                int offset = addr - IOPMP_SRCMD_EN0;
                int idx = offset >> 5;
                offset &= 0x1f;
                if (offset == 0) {
                    rz = s->regs.srcmd_en[idx] & UINT32_MAX;
                } else if (offset == 4) {
                    rz = s->regs.srcmd_en[idx] >> 32;
                } else if (offset == 8) {
                    rz = s->regs.srcmd_r[idx] & UINT32_MAX;
                } else if (offset == 12) {
                    rz = s->regs.srcmd_r[idx] >> 32;
                } else if (offset == 16) {
                    rz = s->regs.srcmd_w[idx] & UINT32_MAX;
                } else if (offset == 24) {
                    rz = s->regs.srcmd_w[idx] >> 32;
                } else {
                    LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
                }
            } else if (addr >= IOPMP_ENTRY_ADDR0 &&
                       addr < IOPMP_USER_CFG0 + 16 * (s->entry_num - 1)) {
                int offset = addr - IOPMP_ENTRY_ADDR0;
                int idx = offset >> 4;
                offset &= 0xf;
                if (offset == 0) {
                    rz = s->regs.entry[idx].addr_reg & UINT32_MAX;
                } else if (offset == 4) {
                    rz = s->regs.entry[idx].addr_reg >> 32;
                } else if (offset == 8) {
                    rz = s->regs.entry[idx].cfg_reg;
                } else if (offset == 12) {
                    /* Not support user customized permission*/
                    rz = 0;
                } else {
                    LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
                }
            } else {
                LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
            }
            break;
        }
        LOG("\e[95m%s: addr %08x, value %08x\e[0m\n", __func__, (int)addr,
            (int)rz);
        break;
    default:
        LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
    }

    return rz;
}

static void
iopmp_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IopmpState *s = IOPMP(opaque);
    int value_f;
    int reg_f;
    uint32_t sid, op;

    switch (addr) {
    case IOPMP_VERSION ... IOPMP_USER_CFG0 + 16 * (IOPMP_MAX_ENTRY_NUM - 1):
        switch (addr) {
        case IOPMP_VERSION: /* RO */
            break;
        case IOPMP_IMP: /* RO */
            break;
        case IOPMP_HWCFG0: /* RO */
            break;
        case IOPMP_HWCFG1:
            if (iopmp_get_field(value, HWCFG1_PRIENT_PROG)) {
                /* W1C */
                s->prient_prog = 0;
            }
            if (iopmp_get_field(value, HWCFG1_SID_TRANSL_PROG)) {
                /* W1C */
                s->sid_transl_prog = 0;
            }
            if (iopmp_get_field(value, HWCFG1_ENABLE)) {
                /* W1S */
                s->enable = 1;
            }
            break;
        case IOPMP_HWCFG2:
            if (s->prient_prog) {
                s->prio_entry = iopmp_get_field(value, HWCFG2_PRIO_ENTRY);
            }
            if (s->sid_transl_en && s->sid_transl_prog) {
                s->sid_transl = iopmp_get_field(value, HWCFG2_SID_TRANSL);
            }
            break;
        case IOPMP_ERRREACT:
            if (!iopmp_get_field(s->regs.errreact, ERRREACT_L)) {
                    iopmp_set_field32(&s->regs.errreact, ERRREACT_L,
                                    iopmp_get_field(value, ERRREACT_L));
                if (iopmp_get_field(value, ERRREACT_IP)) {
                    iopmp_set_field32(&s->regs.errreact, ERRREACT_IP, 0);
                }
                iopmp_set_field32(&s->regs.errreact, ERRREACT_IE,
                                iopmp_get_field(value, ERRREACT_IE));
                iopmp_set_field32(&s->regs.errreact, ERRREACT_IRE,
                                iopmp_get_field(value, ERRREACT_IRE));
                iopmp_set_field32(&s->regs.errreact, ERRREACT_RRE,
                                iopmp_get_field(value, ERRREACT_RRE));
                iopmp_set_field32(&s->regs.errreact, ERRREACT_IWE,
                                iopmp_get_field(value, ERRREACT_IWE));
                iopmp_set_field32(&s->regs.errreact, ERRREACT_RWE,
                                iopmp_get_field(value, ERRREACT_RWE));
                iopmp_set_field32(&s->regs.errreact, ERRREACT_PEE,
                                iopmp_get_field(value, ERRREACT_PEE));
                iopmp_set_field32(&s->regs.errreact, ERRREACT_RPE,
                                iopmp_get_field(value, ERRREACT_RPE));
            } else {
                if (iopmp_get_field(value, ERRREACT_IP)) {
                    iopmp_set_field32(&s->regs.errreact, ERRREACT_IP, 0);
                }
            }
            break;
        case IOPMP_MDSTALL:
            iopmp_set_field64(&s->regs.mdstall, MDSTALL, value);
            break;
        case IOPMP_MDSTALLH:
            iopmp_set_field64(&s->regs.mdstall, MDSTALLH, value);
            break;
        case IOPMP_SIDSCP:
            sid = iopmp_get_field(value, SIDSCP_SID);
            op = iopmp_get_field(value, SIDSCP_OP);
            if (sid < s->sid_num) {
                if (op != SIDSCP_OP_QUERY) {
                    s->sidscp_op[sid] = op;
                    s->regs.sidscp = value;
                }
            } else {
                s->regs.sidscp = sid | (0x3 << SIDSCP_OP);
            }
            break;
        case IOPMP_MDLCK:
            if (!(s->regs.mdlck & (1 << MDLCK_L))) {
                s->regs.mdlck = value |
                                (s->regs.mdstall & ~(uint64_t)UINT32_MAX);
            }
            break;
        case IOPMP_MDLCKH:
             if (!(s->regs.mdlck & (1 << MDLCK_L))) {
                s->regs.mdlck = (uint64_t)value << 32 |
                                (s->regs.mdstall & UINT32_MAX);
            }
            break;
        case IOPMP_MDCFGLCK:
            if (!iopmp_get_field(s->regs.mdcfglck, MDCFGLCK_L)) {
                value_f = iopmp_get_field(value, MDCFGLCK_F);
                reg_f = iopmp_get_field(s->regs.mdcfglck, MDCFGLCK_F);
                if (value_f > reg_f) {
                    iopmp_set_field32(&s->regs.mdcfglck, MDCFGLCK_F, value_f);
                }
                iopmp_set_field32(&s->regs.mdcfglck, MDCFGLCK_L,
                          iopmp_get_field(value, MDCFGLCK_L));
            }
            break;
        case IOPMP_ENTRYLCK:
            if (!(iopmp_get_field(s->regs.entrylck, ENTRYLCK_L))) {
                value_f = iopmp_get_field(value, ENTRYLCK_F);
                reg_f = iopmp_get_field(s->regs.entrylck, ENTRYLCK_F);
                if (value_f > reg_f) {
                    iopmp_set_field32(&s->regs.entrylck, ENTRYLCK_F, value_f);
                }
                iopmp_set_field32(&s->regs.entrylck, ENTRYLCK_F,
                          iopmp_get_field(value, ENTRYLCK_F));
            }
        case IOPMP_ERR_REQADDR: /* RO */
            break;
        case IOPMP_ERR_REQADDRH: /* RO */
            break;
        case IOPMP_ERR_REQSID: /* RO */
            break;
        case IOPMP_ERR_REQINFO: /* RO */
            break;

        default:
            if (addr >= IOPMP_MDCFG0 &&
                addr < IOPMP_MDCFG0 + 4 * (s->md_num - 1)) {
                int offset = addr - IOPMP_MDCFG0;
                int idx = offset >> 2;
                /* RO in rapid-k model */
                if (idx > 0) {
                    LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
                }
            } else if (addr >= IOPMP_SRCMD_EN0 &&
                       addr < IOPMP_SRCMD_WH0 + 32 * (s->sid_num - 1)) {
                int offset = addr - IOPMP_SRCMD_EN0;
                int idx = offset >> 5;
                offset &= 0x1f;
                if (offset % 4) {
                    LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
                } else if (iopmp_get_field(s->regs.srcmd_en[idx],
                                           SRCMD_EN_L) == 0) {
                    if (offset == 0) {
                        iopmp_set_field64(&s->regs.srcmd_en[idx], SRCMD_EN_MD,
                                        iopmp_get_field(value, SRCMD_EN_MD));
                        iopmp_set_field64(&s->regs.srcmd_en[idx], SRCMD_EN_L,
                                        iopmp_get_field(value, SRCMD_EN_L));
                    } else if (offset == 4) {
                        iopmp_set_field64(&s->regs.srcmd_en[idx], SRCMD_ENH_MDH,
                                        value);
                    } else if (offset == 8 && s->sps_en) {
                        iopmp_set_field64(&s->regs.srcmd_r[idx], SRCMD_R_MD,
                                            iopmp_get_field(value, SRCMD_R_MD));
                    } else if (offset == 12 && s->sps_en) {
                        iopmp_set_field64(&s->regs.srcmd_r[idx], SRCMD_RH_MDH,
                                        value);
                    } else if (offset == 16 && s->sps_en) {
                        iopmp_set_field64(&s->regs.srcmd_w[idx], SRCMD_W_MD,
                                        iopmp_get_field(value, SRCMD_W_MD));
                    } else if (offset == 24 && s->sps_en) {
                        iopmp_set_field64(&s->regs.srcmd_w[idx], SRCMD_WH_MDH,
                                          value);
                    }
                }
            } else if (addr >= IOPMP_ENTRY_ADDR0 &&
                       addr < IOPMP_USER_CFG0 + 16 * (s->entry_num - 1)) {
                int offset = addr - IOPMP_ENTRY_ADDR0;
                int idx = offset >> 4;
                offset &= 0xf;
                if (offset == 0) {
                    iopmp_set_field64(&s->regs.entry[idx].addr_reg,
                                      ENTRY_ADDR_ADDR, value);
                } else if (offset == 4) {
                    iopmp_set_field64(&s->regs.entry[idx].addr_reg,
                                      ENTRY_ADDRH_ADDRH, value);
                } else if (offset == 8) {
                    s->regs.entry[idx].cfg_reg = value;
                } else if (offset == 12) {
                    /* Not support user customized permission*/
                    ;
                } else {
                    LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
                }
                iopmp_update_rule(s, idx);
            } else {
                LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
            }
            /* If IOPMP permission of any addr has been changed, */
            /* flush TLB pages. */
            tlb_flush_all_cpus_synced(current_cpu);
            break;
        }
        LOG("\e[95m%s: addr %08x, value %08x\e[0m\n", __func__, (int)addr,
            (int)rz);
        break;
    default:
        LOGGE("%s: Bad addr %x\n", __func__, (int)addr);
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
            if (e_addr >= s->entry_addr[i].sa &&
                e_addr <= s->entry_addr[i].ea) {
                *entry_idx = i;
                return ENTRY_HIT;
            } else if (i >= s->prio_entry) {
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
    uint64_t srcmd_en = s->regs.srcmd_en[sid] >> 1;
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
    uint64_t md_selected = iopmp_get_field(s->regs.mdstall, MDSTALL_MD) &
                           (1 << md_idx);
    if (iopmp_get_field(s->regs.mdstall, MDSTALL_EXEMPT)) {
        return !md_selected;
    } else {
        return md_selected;
    }
}

static inline bool check_sidscp_stall(IopmpState *s, int sid)
{
    return s->sidscp_op[sid] == SIDSCP_OP_STALL;
}

static void iopmp_error_reaction(IopmpState *s, uint32_t id, hwaddr addr,
                                 hwaddr size, uint32_t info)
{
    if (addr > s->prev_error[id].addr_start &&
        addr + size == s->prev_error[id].addr_end &&
        info == s->prev_error[id].reqinfo) {
            /* skip following error */
            ;
    } else {
        s->prev_error[id].addr_start = addr;
        s->prev_error[id].addr_end = addr + size;
        s->prev_error[id].reqinfo = info;
        if (!iopmp_get_field(s->regs.errreact, ERRREACT_IP)) {
            iopmp_set_field32(&s->regs.errreact, ERRREACT_IP, 1);
            s->regs.err_reqsid = id;
            s->regs.err_reqaddr = addr;
            s->regs.err_reqinfo = info;

            if (iopmp_get_field(info, ERR_REQINFO_TYPE) == ERR_REQINFO_TYPE_READ
               && iopmp_get_field(s->regs.errreact, ERRREACT_IE) &&
               iopmp_get_field(s->regs.errreact, ERRREACT_IRE)) {
                qemu_set_irq(s->irq, 1);
            }
            if (iopmp_get_field(info, ERR_REQINFO_TYPE) ==
                ERR_REQINFO_TYPE_WRITE &&
                iopmp_get_field(s->regs.errreact, ERRREACT_IE) &&
                iopmp_get_field(s->regs.errreact, ERRREACT_IWE)) {
                qemu_set_irq(s->irq, 1);
            }
        }
    }
}

static IOMMUTLBEntry iopmp_translate_size(IOMMUMemoryRegion *iommu,
                                          hwaddr addr, hwaddr size,
                                          IOMMUAccessFlags flags,
                                          int sid)
{
    bool is_stalled = false;
    IopmpState *s = IOPMP(container_of(iommu, IopmpState, iommu));
    IOMMUTLBEntry entry = {
        .target_as = &s->downstream_as,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = (~(hwaddr)0),
        .perm = IOMMU_NONE,
    };
    int entry_idx = -1;
    int md_idx = -1;
    int result = match_entry(s, sid, addr, addr + size - 1,
                             &md_idx, &entry_idx);
    int srcmd_rw;
    if (result == ENTRY_HIT) {
        is_stalled = check_md_stall(s, md_idx) || check_sidscp_stall(s, sid);
        if (is_stalled) {
            s->md_stall_stat |= (1 << md_idx);
            entry.target_as = &s->stall_io_as;
            entry.perm = IOMMU_RW;
            return entry;
        } else {
            s->md_stall_stat &= ~(1 << md_idx);
        }
        entry.perm = s->regs.entry[entry_idx].cfg_reg & 0x7;
        if (s->sps_en) {
            /* do not affect x permission */
            srcmd_rw = 0x4 | ((s->regs.srcmd_r[sid] >>
                              (md_idx + SRCMD_R_MD)) & 0x1);
            srcmd_rw |= ((s->regs.srcmd_w[sid] >>
                         (md_idx + SRCMD_W_MD)) & 0x1) << 1;
            entry.perm &= srcmd_rw;
        }
        if ((entry.perm & flags) == 0) {
            /* permission denied */
            iopmp_error_reaction(s, sid, addr, size,
                                 (entry_idx << ERR_REQINFO_EID) |
                                 ((flags - 1) << ERR_REQINFO_TYPE));
            entry.target_as = &s->blocked_io_as;
            entry.perm = IOMMU_RW;
        } else {
            entry.addr_mask = s->entry_addr[entry_idx].ea -
                              s->entry_addr[entry_idx].sa;
            if (s->sid_transl_en) {
                /* next iopmp */
                if (s->next_iommu) {
                    return iopmp_translate_size(s->next_iommu, addr, size,
                                                flags, s->sid_transl);
                } else {
                    error_report("Next iopmp is not found.");
                    exit(1);
                }
            }
        }
    } else {
        if (result == ENTRY_PAR_HIT) {
            iopmp_error_reaction(s, sid, addr, size,
                                 (1 << ERR_REQINFO_PAR_HIT) |
                                 ((flags - 1) << ERR_REQINFO_TYPE));
        } else {
            iopmp_error_reaction(s, sid, addr, size,
                                 (1 << ERR_REQINFO_NO_HIT) |
                                 ((flags - 1) << ERR_REQINFO_TYPE));
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

    switch (iopmp_get_field(s->regs.errreact, ERRREACT_RWE)) {
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

    switch (iopmp_get_field(s->regs.errreact, ERRREACT_RRE)) {
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
    s->downstream = get_system_memory();
    uint64_t size = memory_region_size(s->downstream);

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
        iopmp_set_field32(&s->regs.mdcfglck, MDCFGLCK_F, s->md_num);
        iopmp_set_field32(&s->regs.mdcfglck, MDCFGLCK_L, 1);
        s->regs.mdcfg[0] = s->k;
    } else {
        error_report("IOPMP model %s is not supported. "
                     "Vailid values are full, rapidk, dynamick,"
                     "isolation and compactk.", s->model_str);
        exit(1);
    }
    memory_region_init_iommu(&s->iommu, sizeof(s->iommu),
                             TYPE_IOPMP_IOMMU_MEMORY_REGION,
                             obj, "iopmp-iommu", UINT64_MAX);
    address_space_init(&s->iopmp_as, MEMORY_REGION(&s->iommu), "iommu");
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
}

static void iopmp_reset(DeviceState *dev)
{
    IopmpState *s = IOPMP(dev);
    qemu_set_irq(s->irq, 0);
    memset(&s->regs, 0, sizeof(iopmp_regs));
    memset(&s->entry_addr, 0, IOPMP_MAX_ENTRY_NUM * sizeof(iopmp_addr_t));
    if (s->model == IOPMP_MODEL_RAPIDK) {
        iopmp_set_field32(&s->regs.mdcfglck, MDCFGLCK_F, s->md_num);
        iopmp_set_field32(&s->regs.mdcfglck, MDCFGLCK_L, 1);
        s->regs.mdcfg[0] = s->k;
    }
    s->regs.errreact = 0;

    s->prient_prog = 1;
    if (s->sid_transl_en) {
        s->sid_transl_prog = 1;
    }
}

static int iopmp_attrs_to_index(IOMMUMemoryRegion *iommu, MemTxAttrs attrs)
{
    /* Get source id(SID) */
    return attrs.iopmp_sid;
}


static void iopmp_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate_size = iopmp_translate_size;
    imrc->attrs_to_index = iopmp_attrs_to_index;
}

static Property iopmp_property[] = {
    DEFINE_PROP_STRING("model", IopmpState, model_str),
    DEFINE_PROP_BOOL("sps_en", IopmpState, sps_en, false),
    DEFINE_PROP_BOOL("sid_transl_en", IopmpState, sid_transl_en, false),
    DEFINE_PROP_UINT32("k", IopmpState, k, IOPMP_MODEL_K),
    DEFINE_PROP_UINT32("prio_entry", IopmpState, prio_entry, PRIO_ENTRY),
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

DeviceState *
iopmp_create(hwaddr addr, qemu_irq irq)
{
    LOG("%s:\n", __func__);
    DeviceState *iopmp_device = sysbus_create_varargs(TYPE_IOPMP, addr, irq,
                                                      NULL);
    return iopmp_device;
}

void cascade_iopmp(DeviceState *cur_dev, DeviceState *next_dev)
{
    IopmpState *s = IOPMP(cur_dev);
    s->sid_transl_en = true;
    IopmpState *next_s = IOPMP(next_dev);
    s->next_iommu = &next_s->iommu;
}

static void
iopmp_register_types(void)
{
    type_register_static(&iopmp_info);
    type_register_static(&iopmp_iommu_memory_region_info);
}

type_init(iopmp_register_types);
