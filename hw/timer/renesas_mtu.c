/*
 * Renesas Multi-function Timer Uint
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 * (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2019 Yoshinori Sato
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
#include "qemu-common.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/timer/renesas_mtu.h"
#include "qemu/error-report.h"

REG8(TCR_012, 0)
REG8(TMDR_012, 1)
REG8(TIORH_012, 2)
REG8(TIORL_012, 3)
REG8(TIER_012, 4)
REG8(TSR_012, 5)
REG16(TCNT_012, 6)
REG16(TGRA_012, 8)
REG16(TGRB_012, 10)
REG16(TGRC_012, 12)
REG16(TGRD_012, 14)
REG8(TICCR_1, 16)
REG16(TGRE_0, 32)
REG16(TGRF_0, 34)
REG8(TIER2_0, 36)
REG8(TBTM_0, 38)

REG8(TCR_3, 0)
REG8(TCR_4, 1)
REG8(TMDR_3, 2)
REG8(TMDR_4, 3)
REG8(TIORH_3, 4)
REG8(TIORL_3, 5)
REG8(TIORH_4, 6)
REG8(TIORL_4, 7)
REG8(TIER_3, 8)
REG8(TIER_4, 9)
REG8(TOER, 10)
  FIELD(TOER, OE3B, 0, 1)
  FIELD(TOER, OE4A, 1, 1)
  FIELD(TOER, OE4B, 2, 1)
  FIELD(TOER, OE3D, 3, 1)
  FIELD(TOER, OE4C, 4, 1)
  FIELD(TOER, OE4D, 5, 1)
REG8(TGCR, 13)
  FIELD(TGCR, BDC, 6, 1)
  FIELD(TGCR, N, 5, 1)
  FIELD(TGCR, P, 4, 1)
  FIELD(TGCR, FB, 3, 1)
  FIELD(TGCR, WF, 2, 1)
  FIELD(TGCR, VF, 1, 1)
  FIELD(TGCR, UF, 0, 1)
REG8(TOCR1, 14)
  FIELD(TOCR1, OLSP, 0, 1)
  FIELD(TOCR1, OLSN, 1, 1)
  FIELD(TOCR1, TOCS, 2, 1)
  FIELD(TOCR1, TOCL, 3, 1)
  FIELD(TOCR1, PSYE, 6, 1)
REG8(TOCR2, 15)
  FIELD(TOCR2, OLS1P, 0, 1)
  FIELD(TOCR2, OLS1N, 1, 1)
  FIELD(TOCR2, OLS2P, 2, 1)
  FIELD(TOCR2, OLS2N, 3, 1)
  FIELD(TOCR2, OLS3P, 4, 1)
  FIELD(TOCR2, OLS3N, 5, 1)
  FIELD(TOCR2, BF, 6, 2)
REG16(TCNT_3, 16)
REG16(TCNT_4, 18)
REG16(TCDR, 20)
REG16(TDDR, 22)
REG16(TGRA_3, 24)
REG16(TGRB_3, 26)
REG16(TGRA_4, 28)
REG16(TGRB_4, 30)
REG16(TCNTS, 32)
REG16(TCBR, 34)
REG16(TGRC_3, 36)
REG16(TGRD_3, 38)
REG16(TGRC_4, 40)
REG16(TGRD_4, 42)
REG8(TSR_3, 44)
REG8(TSR_4, 45)
REG8(TITCR, 48)
  FIELD(TITCR, T4VCOR, 0, 3)
  FIELD(TITCR, T4VEN, 3, 1)
  FIELD(TITCR, T3ACOR, 4, 3)
  FIELD(TITCR, T3AEN, 7, 1)
REG8(TITCNT, 49)
  FIELD(TITCNT, T4VCNT, 0, 3)
  FIELD(TITCNT, T3ACNT, 4, 3)
REG8(TBTER, 50)
  FIELD(TBTER, BTE, 0, 2)
REG8(TDER, 52)
  FIELD(TDER, TDER, 0, 1)
REG8(TOLBR, 54)
  FIELD(TOLBR, OLS1P, 0, 1)
  FIELD(TOLBR, OLS1N, 1, 1)
  FIELD(TOLBR, OLS2P, 2, 1)
  FIELD(TOLBR, OLS2N, 3, 1)
  FIELD(TOLBR, OLS3P, 4, 1)
  FIELD(TOLBR, OLS3N, 5, 1)
REG8(TBTM_3, 56)
REG8(TBTM_4, 57)
REG16(TADCR_4, 64)
REG16(TADCORA_4, 68)
REG16(TADCORB_4, 70)
REG16(TADCOBRA_4, 72)
REG16(TADCOBRB_4, 74)
REG8(TWCR, 96)
  FIELD(TWCR, WRE, 0, 1)
  FIELD(TWCR, CCE, 7, 1)
REG8(TSTR, 128)
  FIELD(TSTR, CST0, 0, 1)
  FIELD(TSTR, CST1, 1, 1)
  FIELD(TSTR, CST2, 2, 1)
  FIELD(TSTR, CSTL, 0, 3)
  FIELD(TSTR, CST3, 6, 1)
  FIELD(TSTR, CST4, 7, 1)
  FIELD(TSTR, CSTH, 6, 2)
REG8(TSYR, 129)
  FIELD(TSYR, SYNC0, 0, 1)
  FIELD(TSYR, SYNC1, 1, 1)
  FIELD(TSYR, SYNC2, 2, 1)
  FIELD(TSYR, SYNC3, 6, 1)
  FIELD(TSYR, SYNC4, 7, 1)
REG8(TRWER, 132)
  FIELD(TRWER, RWE, 0, 1)

REG16(TCNTU_5, 0)
REG16(TGRU_5, 2)
REG16(TCRU_5, 4)
REG8(TIORU_5, 6)
REG16(TCNTV_5, 16)
REG16(TGRV_5, 18)
REG8(TCRV_5, 20)
REG8(TIORV_5, 22)
REG16(TCNTW_5, 32)
REG16(TGRW_5, 34)
REG8(TCRW_5, 36)
REG8(TIORW_5, 38)
REG8(TIER_5, 50)
  FIELD(TIER_5, TGIEW5, 0, 1)
  FIELD(TIER_5, TGIEV5, 1, 1)
  FIELD(TIER_5, TGIEU5, 2, 1)
  FIELD(TIER_5, TGIE5,  0, 3)
REG8(TSTR_5, 52)
  FIELD(TSTR_5, CSTW5, 0, 1)
  FIELD(TSTR_5, CSTV5, 1, 1)
  FIELD(TSTR_5, CSTU5, 2, 1)
  FIELD(TSTR_5, CST5,  0, 3)
REG8(TCNTCMPCLR_5, 54)
  FIELD(TCNTCMPCLR_5, CMPCLRW5, 0, 1)
  FIELD(TCNTCMPCLR_5, CMPCLRV5, 1, 1)
  FIELD(TCNTCMPCLR_5, CMPCLRU5, 2, 1)
  FIELD(TCNTCMPCLR_5, CMPCLR5,  0, 3)

REG8(TCR, 1)
  FIELD(TCR, TPSC, 0, 3)
  FIELD(TCR, CKEG, 3, 2)
  FIELD(TCR, CCLR, 5, 3)
REG8(TMDR, 2)
  FIELD(TMDR, MD,  0, 4)
  FIELD(TMDR, BFA, 4, 1)
  FIELD(TMDR, BFB, 5, 1)
  FIELD(TMDR, BFE, 6, 1)
REG16(TIOR, 3)
  FIELD(TIOR, IOA,  0, 3)
  FIELD(TIOR, IOB,  4, 3)
  FIELD(TIOR, IOC,  8, 3)
  FIELD(TIOR, IOD, 12, 3)
REG8(TIOR5, 4)
  FIELD(TIOR5, IOC,  0, 4)
REG8(TCNTCMPCLR, 5)
  FIELD(TCNTCMPCLR, CMPCLR5W, 0, 1)
  FIELD(TCNTCMPCLR, CMPCLR5V, 1, 1)
  FIELD(TCNTCMPCLR, CMPCLR5U, 2, 1)
  FIELD(TCNTCMPCLR, CMPCLR5,  0, 3)
REG8(TIER, 6)
  FIELD(TIER, TGIEA, 0, 1)
  FIELD(TIER, TGIEB, 1, 1)
  FIELD(TIER, TGIEC, 2, 1)
  FIELD(TIER, TGIED, 3, 1)
  FIELD(TIER, TGIE,  0, 4)
  FIELD(TIER, TCIEV, 4, 1)
  FIELD(TIER, TCIEU, 5, 1)
  FIELD(TIER, TTGE2, 6, 1)
  FIELD(TIER, TTGE,  7, 1)
REG8(TIER2, 7)
  FIELD(TIER2, TGIEE, 0, 1)
  FIELD(TIER2, TGIEF, 1, 1)
  FIELD(TIER2, TGIE,  0, 2)
REG8(TSR, 8)
  FIELD(TSR, TCFD, 7, 1)
REG8(TBTM, 9)
  FIELD(TBTM, TTSA, 0, 1)
  FIELD(TBTM, TTSB, 1, 1)
  FIELD(TBTM, TTSE, 2, 1)
REG8(TICCR, 10)
  FIELD(TICCR, I1AE, 0, 1)
  FIELD(TICCR, I1BE, 1, 1)
  FIELD(TICCR, I2AE, 2, 1)
  FIELD(TICCR, I2BE, 3, 1)
REG16(TADCR, 11)
  FIELD(TADCR, ITB4VE, 0, 1)
  FIELD(TADCR, ITB3AE, 1, 1)
  FIELD(TADCR, ITA4VE, 2, 1)
  FIELD(TADCR, ITA3AE, 3, 1)
  FIELD(TADCR, DT4BE,  4, 1)
  FIELD(TADCR, UT4BE,  5, 1)
  FIELD(TADCR, DT4AE,  6, 1)
  FIELD(TADCR, UT4AE,  7, 1)
  FIELD(TADCR, BF,     0, 1)
REG16(TCNT, 12)
REG16(TGRA, 13)
REG16(TGRB, 14)
REG16(TGRC, 15)
REG16(TGRD, 16)
REG16(TGRE, 17)
REG16(TGRF, 18)
REG8(TIORH, 19)
REG8(TIORL, 20)
REG16(TADCOBRA, 21)
REG16(TADCOBRB, 22)
REG16(TADCORA, 23)
REG16(TADCORB, 24)

static const int div_rate[6][8] = {
    [0] = {1, 4, 16, 64, 0, 0, 0, 0, },
    [1] = {1, 4, 16, 64, 0, 0, 256, 0, },
    [2] = {1, 4, 16, 64, 0, 0, 0, 1024, },
    [3] = {1, 4, 16, 64, 256, 1024, 0, 0, },
    [4] = {1, 4, 16, 64, 256, 1024, 0, 0, },
    [5] = {1, 4, 16, 64, 0, 0, 0, 0, },
};

static bool is_cascade(RenesasMTU2State *mtu)
{
    if (mtu == NULL) {
        return false;
    }
    if (FIELD_EX8(mtu->r[1].tcr, TCR, TPSC) != 7 ||
        mtu->r[2].ier) {
        return false;
    }
    return true;
}

static void mtu2_event(void *opaque);
static void set_next_event(RenesasMTURegs *r)
{
    int gr;
    int64_t next;
    uint32_t wcnt;
    int ch = r->ch;
    RenesasMTU2State *mtu = r->mtu;

    if (ch == 1 && is_cascade(mtu)) {
        /* If cascade count mode, skip ch1 event */
        return;
    }
    if (r->start) {
        if (ch != 2 || !is_cascade(mtu)) {
            /* normal counter */
            r->next_cnt = 0x10000;
            for (gr = 0; gr < r->num_gr; gr++) {
                if (r->tcnt <= r->tgr[gr]) {
                    r->next_cnt = MIN(r->next_cnt, r->tgr[gr] + 1);
                }
            }
            next = (r->next_cnt - r->tcnt) * r->clk;
            g_assert(next > 0);
        } else {
            /* 32bit freerun counter */
            wcnt = mtu->r[2].tcnt;
            wcnt = deposit32(wcnt, 16, 16, mtu->r[1].tcnt);
            next = (0x100000000LL - wcnt) * r->clk;
         }
        g_assert(next > 0);
        r->next = r->base + next;
        if (r->timer == NULL) {
            r->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    mtu2_event, r);
        }
        timer_mod(r->timer, r->next);
    } else {
        if (r->timer) {
            timer_del(r->timer);
        }
    }
}

static void mtu2_5_event(void *opaque);
static void set_next_event5(RenesasMTURegs *r)
{
    int64_t next;

    r->next_cnt = r->cntclr ? r->tgr[0] : 0x10000;
    if (r->start) {
        next = (r->next_cnt - r->tcnt) * r->clk;
        g_assert(next > 0);
        r->next = r->base + next;
        if (r->timer == NULL) {
            r->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    mtu2_5_event, r);
        }
        timer_mod(r->timer, r->next);
    } else {
        if (r->timer) {
            timer_del(r->timer);
        }
    }
}

static void tgr_match(RenesasMTURegs *r, int clr_gr)
{
    int gr;

    for (gr = 0; gr < r->num_gr; gr++) {
        if (r->next_cnt == r->tgr[gr]) {
            /* TGR match */
            if (clr_gr == gr) {
                r->tcnt = 0;
            } else {
                r->tcnt = r->next_cnt;
            }
            if (extract16(r->tier, (gr < 4 ? gr : gr + 4), 1)) {
                qemu_irq_pulse(r->irq[gr]);
            }
        }
    }
}

static int clr_gr(uint8_t tcr, int ch)
{
    switch (FIELD_EX8(tcr, TCR, CCLR)) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 5:
        return 2;
    case 6:
        return 3;
    default:
        return -1;
    }
}

static void mtu2_event(void *opaque)
{
    RenesasMTURegs *r = opaque;
    RenesasMTU2State *mtu = r->mtu;
    uint32_t sync;
    int ch;

    if (r->ch != 2 || !is_cascade(mtu)) {
        tgr_match(r, clr_gr(r->tcr, r->ch));
        if (r->next_cnt == 0x10000) {
            /* Count overflow */
            r->tcnt = 0;
            r->base = r->next;
            if (FIELD_EX16(r->tier, TIER, TCIEV)) {
                qemu_irq_pulse(r->irq[r->num_gr]);
            }
            if (r->ch == 2 && FIELD_EX8(mtu->r[1].tcr, TCR, TPSC) == 7) {
                mtu->r[1].tcnt++;
                tgr_match(&mtu->r[1], clr_gr(mtu->r[1].tcr, 1));
                if (mtu->r[1].tcnt >= 0x10000) {
                    mtu->r[1].tcnt = 0;
                    if (FIELD_EX16(mtu->r[1].tier, TIER, TCIEV)) {
                        qemu_irq_pulse(mtu->r[1].irq[mtu->r[1].num_gr]);
                    }
                }
            }
        }
    } else {
        r->tcnt = 0;
        mtu->r[1].tcnt = 0;
        r->base = r->next;
        if (FIELD_EX16(mtu->r[1].tier, TIER, TCIEV)) {
            qemu_irq_pulse(mtu->r[1].irq[mtu->r[1].num_gr]);
        }
    }
    set_next_event(r);
    if (r->tcnt == 0) {
        sync = extract8(mtu->tsyr, 0, 3);
        sync = deposit32(sync, 3, 2, extract8(mtu->tsyr, 6, 2));
        if (extract32(sync, r->ch, 1)) {
            /* Syncronus clear */
            for (ch = 0; ch < 5; ch++) {
                if (ch == r->ch || !extract8(sync, ch, 1)) {
                    continue;
                }
                if ((FIELD_EX8(mtu->r[ch].tcr, TCR, CCLR) & 3) == 3) {
                    mtu->r[ch].tcnt = 0;
                    set_next_event(&mtu->r[ch]);
                }
            }
        }
    }
}

static void mtu2_5_event(void *opaque)
{
    RenesasMTURegs *r = opaque;

    if (r->next_cnt < 0x10000) {
        if (r->ier) {
            qemu_irq_pulse(r->irq[0]);
        }
        if (r->cntclr) {
            r->tcnt = 0;
            r->base = r->next;
        }
    } else {
        r->tcnt = 0;
        r->base = r->next;
    }
    set_next_event5(r);
}

static uint16_t read_tcnt(RenesasMTURegs *r)
{
    int64_t now;
    uint32_t wcnt;

    if (r->start) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (!is_cascade(r->mtu)) {
            if (r->ch == 1 && FIELD_EX8(r->mtu->r[1].tcr, TCR, TPSC) == 7) {
                return r->tcnt;
            } else {
                return (r->tcnt + (now - r->base) / r->clk) & 0xffff;
            }
        } else {
            wcnt = r->mtu->r[2].tcnt;
            wcnt = deposit32(wcnt, 16, 16, r->mtu->r[1].tcnt);
            wcnt += (now - r->mtu->r[2].base) / r->mtu->r[2].clk;
            switch (r->ch) {
            case 1:
                return extract32(wcnt, 16, 16);
            case 2:
                return extract32(wcnt, 0, 16);
            default:
                g_assert_not_reached();
            }
        }
    } else {
        return r->tcnt;
    }
}

static void mtu_pck_update(void *opaque)
{
    RenesasMTU2State *mtu = RenesasMTU2(opaque);
    int ch;
    for (ch = 0; ch < 5; ch++) {
        mtu->r[ch].tcnt = read_tcnt(&mtu->r[ch]);
    }
    for (ch = 0; ch < 3; ch++) {
        mtu->r5[ch].tcnt = read_tcnt(&mtu->r5[ch]);
    }
    mtu->input_freq = clock_get_hz(mtu->pck);
    if (clock_is_enabled(mtu->pck)) {
        for (ch = 0; ch < 5; ch++) {
            set_next_event(&mtu->r[ch]);
        }
        for (ch = 0; ch < 3; ch++) {
            set_next_event5(&mtu->r5[ch]);
        }
    } else {
        for (ch = 0; ch < 5; ch++) {
            if (mtu->r[ch].timer) {
                timer_del(mtu->r[ch].timer);
            }
        }
        for (ch = 0; ch < 3; ch++) {
            if (mtu->r5[ch].timer) {
                timer_del(mtu->r5[ch].timer);
            }
        }
    }
}

static bool mtu2_low_valid_size(hwaddr addr, unsigned size)
{
    if ((A_TCNT_012 <= addr && addr < (A_TGRD_012 + 2)) ||
        (A_TGRE_0 <= addr && addr < (A_TGRF_0 + 2))) {
        if (size == 2) {
            return true;
        }
    } else {
        if (size == 1) {
            return true;
        }
    }
    return false;
}

static uint64_t mtu2_low_read(void *opaque, hwaddr addr, unsigned size)
{
    RenesasMTU2State *mtu = RenesasMTU2(opaque);
    int gr;
    int ch = (addr >> 7) & 3;
    addr &= 0x7f;

    if (!mtu2_low_valid_size(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Invalid access size %d of 0x%"
                      HWADDR_PRIX "\n", size, addr);
        return UINT64_MAX;
    }
    if (!clock_is_enabled(mtu->pck)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unit %d is stopped.\n", mtu->unit);
        return UINT64_MAX;
    }
    switch (addr) {
    case A_TCR_012:
        return mtu->r[ch].tcr;
    case A_TMDR_012:
        return mtu->r[ch].tmdr;
    case A_TIORL_012:
        return extract16(mtu->r[ch].tior, 0, 8);
    case A_TIORH_012:
        return extract16(mtu->r[ch].tior, 8, 8);
    case A_TIER_012:
        return extract16(mtu->r[ch].tier, 0, 8);
    case A_TIER2_0:
        if (ch == 0) {
            return extract16(mtu->r[ch].tier, 8, 8);
        } else {
            goto no_register;
        }
    case A_TSR_012:
        return mtu->r[ch].tsr;
    case A_TBTM_0:
        if (ch == 0) {
            return mtu->tbtm;
        } else {
            goto no_register;
        }
    case A_TICCR_1:
        if (ch == 1) {
            return mtu->ticcr;
        } else {
            goto no_register;
        }
    case A_TCNT_012:
        return read_tcnt(&mtu->r[ch]);
    case A_TGRA_012:
    case A_TGRB_012:
    case A_TGRC_012:
    case A_TGRD_012:
        gr = ((addr - A_TGRA_012) >> 1) & 3;
        if (gr < mtu->r[ch].num_gr) {
            return mtu->r[ch].tgr[gr];
        } else {
            goto no_register;
        }
    case A_TGRE_0:
    case A_TGRF_0:
        if (ch == 0) {
            gr = (((addr - A_TGRE_0) >> 1) & 2) + 4;
            return mtu->r[0].tgr[gr];
        } else {
            goto no_register;
        }
    no_register:
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unknown register %08lx\n",
                      addr);
        return UINT64_MAX;
    }
}

static bool mtu2_high_valid_size(hwaddr addr, unsigned size)
{
    if ((A_TCNT_3 <= addr && addr < (A_TGRD_4 + 2)) ||
        (A_TADCR <= addr && addr < (A_TADCORB + 2)) ||
        (A_TCDR <= addr && addr < (A_TCBR + 2))) {
        if (size == 2) {
            return true;
        }
    } else {
        if (size == 1) {
            return true;
        }
    }
    return false;
}

static uint64_t mtu2_high_read(void *opaque, hwaddr addr, unsigned size)
{
    RenesasMTU2State *mtu = RenesasMTU2(opaque);
    int ch = 3 + (addr & 1);
    int ch_w = 3 + ((addr >> 1) & 1);
    uint32_t ret;

    if (!mtu2_high_valid_size(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Invalid access size %d\n",
                      size);
        return UINT64_MAX;
    }
    if (addr < 0x20 && ((mtu->trwer & 1) == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: register read protected "
                      "0x%" HWADDR_PRIX "\n", addr);
        return UINT64_MAX;
    }
    if (!clock_is_enabled(mtu->pck)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unit %d is stopped.\n", mtu->unit);
        return UINT64_MAX;
    }
    switch (addr) {
    case A_TCR_3:
    case A_TCR_4:
        return mtu->r[ch].tcr;
    case A_TMDR_3:
    case A_TMDR_4:
        return mtu->r[ch].tmdr;
    case A_TIORL_3:
    case A_TIORL_4:
        return extract32(mtu->r[ch_w].tior, 0, 8);
    case A_TIORH_3:
    case A_TIORH_4:
        return extract32(mtu->r[ch_w].tior, 8, 8);
    case A_TIER_3:
    case A_TIER_4:
        return mtu->r[ch].tier;
    case A_TSR_3:
    case A_TSR_4:
        return mtu->r[ch].tsr;
    case A_TCNT_3:
    case A_TCNT_4:
        return read_tcnt(&mtu->r[ch]);
    case A_TGRA_3:
    case A_TGRB_3:
    case A_TGRA_4:
    case A_TGRB_4:
        return mtu->r[3 + ((addr >> 2) & 1)].tgr[(addr >> 1) & 1];
    case A_TGRC_3:
    case A_TGRD_3:
    case A_TGRC_4:
    case A_TGRD_4:
        return mtu->r[3 + ((addr >> 2) & 1)].tgr[2 + ((addr >> 1) & 1)];
    case A_TADCR_4:
        return mtu->tadcr;
    case A_TADCOBRA_4:
    case A_TADCOBRB_4:
        return mtu->tadcobr[(addr >> 1) & 1];
    case A_TADCORA_4:
    case A_TADCORB_4:
        return mtu->tadcor[(addr >> 1) & 1];
    case A_TOER:
        return mtu->toer;
    case A_TGCR:
        return mtu->tgcr;
    case A_TOCR1:
    case A_TOCR2:
        return mtu->tocr[addr & 1];
    case A_TCDR:
        return mtu->tcdr;
    case A_TDDR:
        return mtu->tddr;
    case A_TCNTS:
        return mtu->tcnts;
    case A_TCBR:
        return mtu->tcbr;
    case A_TITCR:
        return mtu->titcr;
    case A_TITCNT:
        return mtu->titcnt;
    case A_TBTER:
        return mtu->tbter;
    case A_TDER:
        return mtu->tder;
    case A_TOLBR:
        return mtu->tolbr;
    case A_TWCR:
        return mtu->twcr;
    case A_TSTR:
        ret = 0;
        for (ch = 0; ch < 5; ch++) {
            ret = deposit32(ret, (ch < 3 ? ch : ch + 3), 1, mtu->r[ch].start);
        }
        return ret;
    case A_TSYR:
        return mtu->tsyr;
    case A_TRWER:
        mtu->trwer_r = mtu->trwer;
        return mtu->trwer;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unknown register 0x%" HWADDR_PRIX "\n",
                      addr);
        return UINT64_MAX;
    }
}

static bool mtu2_5_valid_size(hwaddr addr, unsigned size)
{
    if (addr < A_TIER_5) {
        addr &= 0x0f;
        if (addr < A_TCRU_5) {
            if (size == 2) {
                return true;
            }
        } else {
            if (size == 1) {
                return true;
            }
        }
    } else {
        if (size == 1) {
            return true;
        }
    }
    return false;
}

static uint64_t mtu2_5_read(void *opaque, hwaddr addr, unsigned size)
{
    RenesasMTU2State *mtu = RenesasMTU2(opaque);
    int ch;
    uint32_t ret;
    ch = addr >> 4;
    if (!mtu2_5_valid_size(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Invalid access size at "
                      "0x%" HWADDR_PRIX "\n", addr);
    }
    if (!clock_is_enabled(mtu->pck)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unit %d is stopped.\n", mtu->unit);
        return UINT64_MAX;
    }
    if (ch < 3) {
        switch (addr & 0x0f) {
        case A_TCNTU_5:
            return read_tcnt(&mtu->r5[ch]);
        case A_TGRU_5:
            return mtu->r5[ch].tgr[0];
        case A_TCRU_5:
            return mtu->r5[ch].tcr;
        case A_TIORU_5:
            return mtu->r5[ch].tior;
        }
    } else {
        switch (addr) {
        case A_TIER_5:
            ret = 0;
            for (ch = 0; ch < 3; ch++) {
                ret = deposit32(ret, ch, 1, mtu->r5[ch].ier);
            }
            return ret;
        case A_TSTR_5:
            ret = 0;
            for (ch = 0; ch < 3; ch++) {
                ret = deposit32(ret, ch, 1, mtu->r5[ch].start);
            }
            return ret;
        case A_TCNTCMPCLR_5:
            ret = 0;
            for (ch = 0; ch < 3; ch++) {
                ret = deposit32(ret, ch, 1, mtu->r5[ch].cntclr);
            }
            return ret;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "renesas_mtu: Unknown register "
                  "0x%" HWADDR_PRIX "\n", addr);
    return UINT64_MAX;
}

static bool is_ext_clock(int ch, int tcr)
{
    int tpsc = FIELD_EX8(tcr, TCR, TPSC);
    if (ch == 1 && tcr == 7) {
        return false;
    } else {
        return div_rate[ch][tpsc] == 0;
    }
}

static void set_cnt_clock(int64_t input_freq, RenesasMTURegs *r)
{
    int tpsc = FIELD_EX8(r->tcr, TCR, TPSC);
    int ckeg = FIELD_EX8(r->tcr, TCR, CKEG);
    int div = div_rate[r->ch][tpsc];
    int64_t clk;

    if (div >= 4 && ckeg >= 2) {
        div /= 2;
    }
    if (div > 0) {
        clk = NANOSECONDS_PER_SECOND / input_freq;
        r->clk = clk * div;
    }
}

#define NOT_SUPPORT_REG_VAL(val, name)                                  \
    if (val != 0) {                                                     \
        qemu_log_mask(LOG_UNIMP,                                        \
                      "renesas_mtu: " #name " %02x is not supported.\n", \
                      (uint8_t)val);                                    \
    }

static void mtu2_low_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    RenesasMTU2State *mtu = RenesasMTU2(opaque);
    int ch = (addr >> 7) & 3;
    addr &= 0x7f;
    if (!mtu2_low_valid_size(addr, size)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "renesas_mtu: Invalid access size %d of "
                          "0x%" HWADDR_PRIX "\n", size, addr);
            return;
    }
    if (!clock_is_enabled(mtu->pck)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unit %d is stopped.\n", mtu->unit);
        return;
    }

    switch (addr) {
    case A_TCR_012:
        if (mtu->r[ch].start) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "renesas_mtu: CH %d is already started.\n", ch);
        }
        if (is_ext_clock(ch, val)) {
            qemu_log_mask(LOG_UNIMP,
                          "renesas_mtu: External clock not supported.\n");
        }
        mtu->r[ch].tcr = val;
        set_cnt_clock(mtu->input_freq, &mtu->r[ch]);
        set_next_event(&mtu->r[ch]);
        break;
    case A_TMDR_012:
        mtu->r[ch].tmdr = val;
        break;
    case A_TIORL_012:
        mtu->r[ch].tior = deposit32(mtu->r[ch].tior, 0, 8, val);
        NOT_SUPPORT_REG_VAL(val, TIORL);
        break;
    case A_TIORH_012:
        mtu->r[ch].tior = deposit32(mtu->r[ch].tior, 8, 8, val);
        NOT_SUPPORT_REG_VAL(val, TIORH);
        break;
    case A_TIER_012:
        mtu->r[ch].tier = deposit32(mtu->r[ch].tier, 0, 8, val);
        break;
    case A_TIER2_0:
        if (ch == 0) {
            mtu->r[ch].tier = deposit32(mtu->r[ch].tior, 8, 8, val);
        } else {
            goto no_register;
        }
        break;
    case A_TSR_012:
        mtu->r[ch].tsr = deposit32(mtu->r[ch].tsr, 6, 1, extract32(val, 6, 1));
        break;
    case A_TBTM_0:
        if (ch == 0) {
            mtu->tbtm = val;
            break;
        } else {
            goto no_register;
        }
    case A_TICCR_1:
        if (ch == 1) {
            mtu->ticcr = val;
            break;
        } else {
            goto no_register;
        }
    case A_TCNT_012:
        mtu->r[ch].tcnt = val;
        if (mtu->r[ch].start) {
            mtu->r[ch].base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        set_next_event(&mtu->r[ch]);
        break;
    case A_TGRA_012:
    case A_TGRB_012:
    case A_TGRC_012:
    case A_TGRD_012:
        mtu->r[ch].tgr[((addr - A_TGRA_012) >> 1) & 3] = val;
        set_next_event(&mtu->r[ch]);
        break;
    case A_TGRE_0:
    case A_TGRF_0:
        if (ch == 0) {
            mtu->r[ch].tgr[(((addr - A_TGRE_0) >> 1) & 2) + 4] = val;
            set_next_event(&mtu->r[ch]);
            break;
        } else {
            goto no_register;
        }
    default:
    no_register:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unknown register 0x%" HWADDR_PRIX "\n",
                      addr);
        break;
    }
}

static void mtu2_high_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    RenesasMTU2State *mtu = RenesasMTU2(opaque);
    int ch = 3 + (addr & 1);
    int ch_w = 3 + ((addr >> 1) & 1);
    int64_t now;

    if (!mtu2_high_valid_size(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Invalid access size %d\n",
                      size);
        return;
    }
    if (addr < 0x20 && ((mtu->trwer & 1) == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: register write protected "
                      "0x%" HWADDR_PRIX "\n", addr);
        return;
    }
    if (!clock_is_enabled(mtu->pck)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unit %d is stopped.\n", mtu->unit);
        return;
    }

    switch (addr) {
    case A_TCR_3:
    case A_TCR_4:
        if (mtu->r[ch].start) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "renesas_mtu: CH %d is already started.\n", ch);
        }
        if (is_ext_clock(ch, val)) {
            qemu_log_mask(LOG_UNIMP,
                          "renesas_mtu: External clock not supported.\n");
        }
        mtu->r[ch].tcr = val;
        set_cnt_clock(mtu->input_freq, &mtu->r[ch]);
        set_next_event(&mtu->r[ch]);
        break;
    case A_TMDR_3:
    case A_TMDR_4:
        mtu->r[ch].tmdr = val;
        NOT_SUPPORT_REG_VAL(val, TMDR);
        break;
    case A_TIORL_3:
    case A_TIORL_4:
        mtu->r[ch_w].tior = deposit32(mtu->r[ch_w].tior, 0, 8, val);
        NOT_SUPPORT_REG_VAL(val, TIORL);
        break;
    case A_TIORH_3:
    case A_TIORH_4:
        mtu->r[ch_w].tior = deposit32(mtu->r[ch_w].tior, 8, 8, val);
        NOT_SUPPORT_REG_VAL(val, TIORH);
        break;
    case A_TIER_3:
    case A_TIER_4:
        mtu->r[ch].tier = val;
        set_next_event(&mtu->r[ch]);
        break;
    case A_TSR_3:
    case A_TSR_4:
        mtu->r[ch].tsr = val;
        break;
    case A_TCNT_3:
    case A_TCNT_4:
        mtu->r[ch_w].tcnt = val;
        if (mtu->r[ch].start) {
            mtu->r[ch].base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }
        set_next_event(&mtu->r[ch]);
        break;
    case A_TGRA_3:
    case A_TGRA_4:
    case A_TGRB_3:
    case A_TGRB_4:
        mtu->r[3 + ((addr >> 2) & 1)].tgr[(addr >> 1) & 1] = val;
        set_next_event(&mtu->r[3 + ((addr >> 2) & 1)]);
        break;
    case A_TGRC_3:
    case A_TGRD_3:
    case A_TGRC_4:
    case A_TGRD_4:
        mtu->r[3 + ((addr >> 2) & 1)].tgr[2 + ((addr >> 1) & 1)] = val;
        set_next_event(&mtu->r[3 + ((addr >> 2) & 1)]);
        break;
    case A_TADCR_4:
        mtu->tadcr = val;
        NOT_SUPPORT_REG_VAL(val, TADCR);
        break;
    case A_TADCOBRA_4:
    case A_TADCOBRB_4:
        mtu->tadcobr[(addr >> 1) & 1] = val;
    case A_TADCORA_4:
    case A_TADCORB_4:
        mtu->tadcor[(addr >> 1) & 1] = val;
    case A_TOER:
        mtu->toer = val;
        break;
    case A_TGCR:
        mtu->tgcr = val;
        break;
    case A_TOCR1:
    case A_TOCR2:
        mtu->tocr[addr & 1] = val;
        break;
    case A_TCDR:
        mtu->tcdr = val;
        break;
    case A_TDDR:
        mtu->tddr = val;
        break;
    case A_TCNTS:
        mtu->tcnts = val;
        break;
    case A_TCBR:
        mtu->tcbr = val;
        break;
    case A_TITCR:
        mtu->titcr = val;
        break;
    case A_TITCNT:
        mtu->titcnt = val;
        break;
    case A_TBTER:
        mtu->tbter = val;
        break;
    case A_TDER:
        mtu->tder = val;
        break;
    case A_TOLBR:
        mtu->tolbr = val;
        break;
    case A_TWCR:
        mtu->twcr = val;
        break;
    case A_TSTR:
        val = deposit64(val, 3, 2, extract64(val, 6, 2));
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        for (ch = 0; ch < 5; ch++) {
            if (mtu->r[ch].start != extract32(val, ch, 1)) {
                mtu->r[ch].start = extract32(val, ch, 1);
                if (mtu->r[ch].start) {
                    mtu->r[ch].base = now;
                }
                set_next_event(&mtu->r[ch]);
            }
        }
        break;
    case A_TSYR:
        mtu->tsyr = val;
        break;
    case A_TRWER:
        if (mtu->trwer_r) {
            mtu->trwer = FIELD_DP8(mtu->trwer, TRWER, RWE,
                                   FIELD_EX8(val, TRWER, RWE));
            mtu->trwer_r = 0;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "renesas_mtu: TRWER protected.\n");
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unknown register "
                      "0x%" HWADDR_PRIX "\n", addr);
        break;
    }
}

static void mtu2_5_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RenesasMTU2State *mtu = RenesasMTU2(opaque);
    int ch;
    int64_t now;

    ch = addr >> 4;
    if (!mtu2_5_valid_size(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Invalid access size at "
                      "0x%" HWADDR_PRIX "\n", addr);
        return;
    }
    if (!clock_is_enabled(mtu->pck)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_mtu: Unit %d is stopped.\n", mtu->unit);
        return;
    }
    if (ch < 3) {
        switch (addr & 0x0f) {
        case A_TCNTU_5:
            mtu->r5[ch].tcnt = val;
            set_next_event5(&mtu->r5[ch]);
            break;
        case A_TGRU_5:
            mtu->r5[ch].tgr[0] = val;
            set_next_event5(&mtu->r5[ch]);
            break;
        case A_TCRU_5:
            mtu->r5[ch].tcr = val;
            set_next_event5(&mtu->r5[ch]);
            break;
        case A_TIORU_5:
            mtu->r5[ch].tior = val;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "renesas_mtu: Unknown register 0x%"
                          HWADDR_PRIX "\n", addr);
            break;
        }
    } else {
        switch (addr & 0xff) {
        case A_TIER_5:
            for (ch = 0; ch < 3; ch++) {
                mtu->r5[ch].ier = extract64(val, ch, 1);
            }
            break;
        case A_TSTR_5:
            now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            for (ch = 0; ch < 3; ch++) {
                if (mtu->r5[ch].start != extract64(val, ch, 1)) {
                    mtu->r5[ch].start = extract64(val, ch, 1);
                    if (mtu->r5[ch].start) {
                        mtu->r5[ch].base = now;
                    }
                    set_next_event5(&mtu->r5[ch]);
                }
            }
            break;
        case A_TCNTCMPCLR_5:
            for (ch = 0; ch < 3; ch++) {
                if (mtu->r5[ch].cntclr != extract64(val, ch, 1)) {
                    mtu->r5[ch].cntclr = extract64(val, ch, 1);
                    set_next_event5(&mtu->r5[ch]);
                }
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "renesas_mtu: Unknown register %08lx\n",
                          addr);
            break;
        }
    }
}

static const MemoryRegionOps mtu2_low_ops = {
    .write = mtu2_low_write,
    .read  = mtu2_low_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static const MemoryRegionOps mtu2_high_ops = {
    .write = mtu2_high_write,
    .read  = mtu2_high_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static const MemoryRegionOps mtu2_5_ops = {
    .write = mtu2_5_write,
    .read  = mtu2_5_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void mtu_reg_init(int channel, RenesasMTU2State *mtu, RenesasMTURegs *r)
{
    int grn;
    static const int gr[] = {6, 2, 2, 4, 4};
    r->ch = channel;
    r->mtu = mtu;
    r->tsr = 0xc0;
    r->num_gr = gr[channel];
    for (grn = 0; grn < r->num_gr; grn++) {
        r->tgr[grn] = 0xffff;
    }
}

static void mtu2_realize(DeviceState *dev, Error **errp)
{
    int ch;
    RenesasMTU2State *mtu = RenesasMTU2(dev);

    for (ch = 0; ch < 5; ch++) {
        mtu_reg_init(ch, mtu, &mtu->r[ch]);
        if (clock_is_enabled(mtu->pck)) {
            set_cnt_clock(mtu->input_freq, &mtu->r[ch]);
        }
    }
    for (ch = 0; ch < 3; ch++) {
        mtu->r5[ch].mtu = NULL;
        mtu->r5[ch].tgr[0] = 0xffff;
        if (clock_is_enabled(mtu->pck)) {
            set_cnt_clock(mtu->input_freq, &mtu->r5[ch]);
        }
    }
    mtu->ticcr = 0x00;
    mtu->toer = 0xc0;
    mtu->tgcr = 0x80;
    mtu->tcdr = mtu->tddr = 0xffff;
    mtu->tcbr = 0xffff;
    mtu->tder = 0x01;
    mtu->trwer = 0x01;
}

static void mtu2_init(Object *obj)
{
    int ch, irq;
    static int nr_irq[] = {7, 4, 4, 5, 5};
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RenesasMTU2State *mtu = RenesasMTU2(obj);

    memory_region_init_io(&mtu->memory[0], OBJECT(mtu), &mtu2_low_ops,
                          mtu, "renesas-mtu2-low", 0x180);
    sysbus_init_mmio(d, &mtu->memory[0]);
    memory_region_init_io(&mtu->memory[1], OBJECT(mtu), &mtu2_high_ops,
                          mtu, "renesas-mtu2-high", 0x90);
    sysbus_init_mmio(d, &mtu->memory[1]);
    memory_region_init_io(&mtu->memory[2], OBJECT(mtu), &mtu2_5_ops,
                          mtu, "renesas-mtu2-5", 0x40);
    sysbus_init_mmio(d, &mtu->memory[2]);
    for (ch = 0; ch < 5; ch++) {
        for (irq = 0; irq < nr_irq[ch]; irq++) {
            sysbus_init_irq(d, &mtu->r[ch].irq[irq]);
        }
    }
    for (ch = 0; ch < 3; ch++) {
        sysbus_init_irq(d, &mtu->r5[ch].irq[0]);
    }
    mtu->pck = qdev_init_clock_in(DEVICE(d), "pck",
                                  mtu_pck_update, mtu);
}

static Property mtu_properties[] = {
    DEFINE_PROP_UINT32("unit", RenesasMTU2State, unit, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void mtu2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mtu2_realize;
    device_class_set_props(dc, mtu_properties);
}

static const TypeInfo renesas_mtu_info = {
    .name       = TYPE_RENESAS_MTU2,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasMTU2State),
    .instance_init = mtu2_init,
    .class_init = mtu2_class_init,
    .class_size = sizeof(RenesasMTU2Class),
};

static void mtu_register_types(void)
{
    type_register_static(&renesas_mtu_info);
}

type_init(mtu_register_types)
