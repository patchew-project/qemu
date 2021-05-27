/*
 * RX62N Clock Generation Circuit
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 * (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2020 Yoshinori Sato
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
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/hw.h"
#include "hw/rx/rx62n-cpg.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/clock.h"
#include "migration/vmstate.h"

#define RX62N_XTAL_MIN_HZ  (8 * 1000 * 1000)
#define RX62N_XTAL_MAX_HZ (14 * 1000 * 1000)

REG32(MSTPCRA, 0)
REG32(MSTPCRB, 4)
REG32(MSTPCRC, 8)
REG32(SCKCR, 16)
  FIELD(SCKCR, PCK,  8, 3)
  FIELD(SCKCR, BCK, 16, 3)
  FIELD(SCKCR, PSTOP, 22, 2)
  FIELD(SCKCR, ICK, 24, 3)
REG8(BCKCR, 32)
  FIELD(BCKCR, BCLKDIV, 0, 1)
REG16(OSTDCR, 48)
  FIELD(OSTDCR, OSTDF, 6, 1)
  FIELD(OSTDCR, OSTDE, 7, 1)

static const int access_size[] = {4, 4, 1, 2};

typedef struct {
    const char *name;
    int devnum;
    int reg;
    int offset;
    int parentck;
} dev_clock_t;

enum {
    parent_ick, parent_bck, parent_pck,
};

static const dev_clock_t dev_clock_list[] = {
    { .name = "pck_tmr8-1",
      .devnum = CK_TMR8_1, .reg = 0, .offset = 4, .parentck = parent_pck, },
    { .name = "pck_tmr8-0",
      .devnum = CK_TMR8_0, .reg = 0, .offset = 5, .parentck = parent_pck, },
    { .name = "pck_mtu-1",
      .devnum = CK_MTU_1, .reg = 0, .offset = 8, .parentck = parent_pck, },
    { .name = "pck_mtu-0",
      .devnum = CK_MTU_0, .reg = 0, .offset = 9, .parentck = parent_pck, },
    { .name = "pck_cmt-1",
      .devnum = CK_CMT_1, .reg = 0, .offset = 14, .parentck = parent_pck, },
    { .name = "pck_cmt-0",
      .devnum = CK_CMT_0, .reg = 0, .offset = 15, .parentck = parent_pck, },
    { .name = "ick_edmac",
      .devnum = CK_EDMAC, .reg = 1, .offset = 15, .parentck = parent_ick, },
    { .name = "pck_sci-6",
      .devnum = CK_SCI6, .reg = 1, .offset = 25, .parentck = parent_pck, },
    { .name = "pck_sci-5",
      .devnum = CK_SCI5, .reg = 1, .offset = 26, .parentck = parent_pck, },
    { .name = "pck_sci-3",
      .devnum = CK_SCI3, .reg = 1, .offset = 28, .parentck = parent_pck, },
    { .name = "pck_sci-2",
      .devnum = CK_SCI2, .reg = 1, .offset = 29, .parentck = parent_pck, },
    { .name = "pck_sci-1",
      .devnum = CK_SCI1, .reg = 1, .offset = 30, .parentck = parent_pck, },
    { .name = "pck_sci-0",
      .devnum = CK_SCI0, .reg = 1, .offset = 31, .parentck = parent_pck, },
    { },
};

static void set_clock_in(RX62NCPGState *cpg, const dev_clock_t *ck)
{
    Clock *out;
    uint64_t period;

    out = qdev_get_clock_out(DEVICE(cpg), ck->name);
    g_assert(out);
    period = 0;
    if (extract32(cpg->mstpcr[ck->reg], ck->offset, 1) == 0) {
        switch (ck->parentck) {
        case parent_ick:
            period = clock_get(cpg->clk_ick);
            break;
        case parent_pck:
            period = clock_get(cpg->clk_pck);
            break;
        }
    }
    if (clock_get(out) != period) {
        clock_update(out, period);
    }
}

#define update_ck(ckname)                                             \
    if (cpg->ckname != ckname) {                                      \
        cpg->ckname = ckname;                                         \
        ckname =  8 / (1 << ckname);                                  \
        clock_update_hz(cpg->clk_ ## ckname,                          \
                        cpg->xtal_freq_hz * ckname);                  \
    }

#define validate_setting(ckname)                                 \
    if (ick > ckname) {                                         \
        qemu_log_mask(LOG_GUEST_ERROR,                           \
                      "rx62n-cpg: Invalid " #ckname " setting."   \
                      " (ick=%d " #ckname "=%d)\n", ick, ckname); \
        cpg->ckname = ckname = ick;                              \
    }

static void update_divrate(RX62NCPGState *cpg)
{
    int ick = FIELD_EX32(cpg->sckcr, SCKCR, ICK);
    int bck = FIELD_EX32(cpg->sckcr, SCKCR, BCK);
    int pck = FIELD_EX32(cpg->sckcr, SCKCR, PCK);
    const dev_clock_t *p = dev_clock_list;
    validate_setting(pck);
    validate_setting(bck);
    update_ck(ick);
    update_ck(bck);
    update_ck(pck);
    while (p->name) {
        set_clock_in(cpg, p);
        p++;
    }
}

static const dev_clock_t *find_clock_list(int crno, int bit)
{
    const dev_clock_t *ret = dev_clock_list;
    while (ret->name) {
        if (ret->reg == crno && ret->offset == bit) {
            return ret;
        }
        ret++;
    }
    return NULL;
}

static void update_mstpcr(RX62NCPGState *cpg, int crno, uint32_t diff)
{
    int bit = 0;
    const dev_clock_t *p;

    while (diff) {
        if (diff & 1) {
            p = find_clock_list(crno, bit);
            if (p) {
                set_clock_in(cpg, p);
            } else {
                qemu_log_mask(LOG_UNIMP, "rx62n-cpg: MSTPCR%c "
                              " bit %d is not implement.\n", 'A' + crno, bit);
            }
        }
        bit++;
        diff >>= 1;
    }
}

static uint64_t cpg_read(void *opaque, hwaddr addr, unsigned size)
{
    RX62NCPGState *cpg = RX62NCPG(opaque);

    if (access_size[addr >> 4] != size) {
        qemu_log_mask(LOG_GUEST_ERROR, "rx62n-cpg: Register 0x%"
                      HWADDR_PRIX " Invalid access size.\n", addr);
        return UINT64_MAX;
    }
    switch (addr) {
    case A_MSTPCRA:
        return cpg->mstpcr[0] | 0x473530cf;
    case A_MSTPCRB:
        return cpg->mstpcr[1] | 0x09407ffe;
    case A_MSTPCRC:
        return (cpg->mstpcr[2] | 0xffff0000) & 0xffff0003;
    case A_SCKCR:
        return cpg->sckcr & 0x0fcf0f00;
    case A_BCKCR:
        return cpg->bckcr & 0x01;
    case A_OSTDCR:
        /* Main OSC always good */
        return cpg->ostdcr & 0x0080;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "rx62n-cpg: Register 0x%"
                      HWADDR_PRIX " Invalid address.\n", addr);
        return UINT64_MAX;
    }
}

static void cpg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RX62NCPGState *cpg = RX62NCPG(opaque);
    uint32_t old_mstpcr;
    int cr_no;
    if (access_size[addr >> 4] != size) {
        qemu_log_mask(LOG_GUEST_ERROR, "rx62n-cpg: Register 0x%"
                      HWADDR_PRIX " Invalid access size.\n", addr);
        return;
    }
    switch (addr) {
    case A_MSTPCRA:
    case A_MSTPCRB:
    case A_MSTPCRC:
        cr_no = (addr & 0x0f) >> 2;
        old_mstpcr = cpg->mstpcr[cr_no];
        old_mstpcr ^= val;
        cpg->mstpcr[cr_no] = val;
        update_mstpcr(cpg, cr_no, old_mstpcr);
        break;
    case A_SCKCR:
        cpg->sckcr = val;
        update_divrate(cpg);
        break;
    case A_BCKCR:
        cpg->bckcr = val;
        break;
    case A_OSTDCR:
        if (extract16(val, 8, 8) == OSTDCR_KEY) {
            cpg->ostdcr = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "rx62n-cpg: Register 0x%"
                          HWADDR_PRIX " Invalid key value.\n", addr);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "rx62n-cpg: Register 0x%"
                      HWADDR_PRIX " Invalid address.\n", addr);
    }
}

static const MemoryRegionOps cpg_ops = {
    .write = cpg_write,
    .read  = cpg_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const ClockPortInitArray rx62n_cpg_clocks = {
    QDEV_CLOCK_OUT(RX62NCPGState, clk_ick),
    QDEV_CLOCK_OUT(RX62NCPGState, clk_bck),
    QDEV_CLOCK_OUT(RX62NCPGState, clk_pck),
    QDEV_CLOCK_END
};

static void cpg_realize(DeviceState *dev, Error **errp)
{
    RX62NCPGState *cpg = RX62NCPG(dev);
    const dev_clock_t *p = dev_clock_list;

    if (cpg->xtal_freq_hz == 0) {
        error_setg(errp, "\"xtal-frequency-hz\" property must be provided.");
        return;
    }
    /* XTAL range: 8-14 MHz */
    if (cpg->xtal_freq_hz < RX62N_XTAL_MIN_HZ ||
        cpg->xtal_freq_hz > RX62N_XTAL_MAX_HZ) {
        error_setg(errp, "\"xtal-frequency-hz\" property in incorrect range.");
        return;
    }

    cpg->sckcr = FIELD_DP32(cpg->sckcr, SCKCR, ICK, 2);
    cpg->sckcr = FIELD_DP32(cpg->sckcr, SCKCR, BCK, 2);
    cpg->sckcr = FIELD_DP32(cpg->sckcr, SCKCR, PCK, 2);
    cpg->ostdcr = FIELD_DP8(cpg->ostdcr, OSTDCR, OSTDE, 1);
    cpg->mstpcr[0] = 0x47ffffff;
    cpg->mstpcr[1] = 0xffffffff;
    cpg->mstpcr[2] = 0xffff0000;

    /* set initial state */
    while (p->name) {
        set_clock_in(cpg, p);
        p++;
    }
    update_divrate(cpg);
}

static void rx62n_cpg_init(Object *obj)
{
    RX62NCPGState *cpg = RX62NCPG(obj);
    const dev_clock_t *p = dev_clock_list;
    qdev_init_clocks(DEVICE(obj), rx62n_cpg_clocks);
    /* connect parent clock */
    while (p->name) {
        cpg->dev_clocks[p->devnum] = qdev_init_clock_out(DEVICE(obj),
                                                         p->name);
        p++;
    }

    memory_region_init_io(&cpg->memory, OBJECT(cpg), &cpg_ops,
                          cpg, "rx62n-cpg", 0x40);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &cpg->memory);
}

static Property rx62n_cpg_properties[] = {
    DEFINE_PROP_UINT32("xtal-frequency-hz", RX62NCPGState, xtal_freq_hz, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void rx62n_cpg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cpg_realize;
    device_class_set_props(dc, rx62n_cpg_properties);
}

static const TypeInfo rx62n_cpg_info[] = {
    {
        .name       = TYPE_RX62N_CPG,
        .parent     = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RX62NCPGState),
        .instance_init = rx62n_cpg_init,
        .class_init = rx62n_cpg_class_init,
        .class_size = sizeof(RX62NCPGClass),
    },
};

DEFINE_TYPES(rx62n_cpg_info)
