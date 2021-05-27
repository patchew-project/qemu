/*
 * SH7750 / SH7751 Clock Generation Circuit
 *
 * Datasheet: SH7751 Group, SH7751R Group User's Manual: Hardware
 *            (Rev.4.01 R01UH0457EJ0401)
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
#include "hw/sh4/sh7751-cpg.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/clock.h"
#include "migration/vmstate.h"

#define SH7751_XTAL_MIN_HZ  (1 * 1000 * 1000)
#define SH7751_XTAL_MAX_HZ (34 * 1000 * 1000)

REG16(FREQCR, 0)
  FIELD(FREQCR, PFC, 0, 3)
  FIELD(FREQCR, BFC, 3, 3)
  FIELD(FREQCR, IFC, 6, 3)
  FIELD(FREQCR, PLL2EN, 9, 1)
  FIELD(FREQCR, PLL1EN, 10, 1)
  FIELD(FREQCR, CKOEN, 11, 1)
REG8(STBCR, 4)
REG8(STBCR2, 16)

REG32(CLKSTP00, 0)
REG32(CLKSTPCLR00, 8)

typedef struct {
    const char *name;
    int devnum;
    int reg;
    int offset;
} dev_clock_t;

static const dev_clock_t dev_clock_list[] = {
    { .name = "pck_sci", .devnum = CK_SCI, .reg = 0, .offset = 0},
    { .name = "pck_rtc", .devnum = CK_RTC, .reg = 0, .offset = 1},
    { .name = "pck_tmu-0", .devnum = CK_TMU_0, .reg = 0, .offset = 2},
    { .name = "pck_scif",  .devnum = CK_SCIF, .reg = 0, .offset = 3},
    { .name = "pck_dmac", .devnum = CK_DMAC, .reg = 0, .offset = 4},
    { .name = "pck_ubc", .devnum = CK_UBC, .reg = 1, .offset = 0},
    { .name = "pck_sq", .devnum = CK_SQ, .reg = 1, .offset = 1},
    { .name = "pck_intc", .devnum = CK_INTC, .reg = 2, .offset = 0},
    { .name = "pck_tmu-1", .devnum = CK_TMU_1, .reg = 2, .offset = 1},
    { .name = "pck_pcic", .devnum = CK_PCIC, .reg = 2, .offset = 2},
    { },
};

static void set_clock_in(SH7751CPGBaseState *cpg, const dev_clock_t *ck)
{
    Clock *out;
    uint64_t period;

    out = qdev_get_clock_out(DEVICE(cpg), ck->name);
    g_assert(out);
    period = 0;
    switch (ck->reg) {
    case 0:
    case 1:
        if (extract8(cpg->stbcr[ck->reg], ck->offset, 1) == 0) {
            period = clock_get(cpg->clk_ick);
        }
        break;
    case 2:
        if (extract32(cpg->clkstp00, ck->offset, 1) == 0) {
            period = clock_get(cpg->clk_ick);
        }
        break;
    }
    if (clock_get(out) != period) {
        clock_update(out, period);
    }
}

static void update_divrate(SH7751CPGBaseState *cpg)
{
    SH7751CPGBaseClass *k = SH7751CPG_GET_CLASS(cpg);
    int ick = FIELD_EX32(cpg->freqcr, FREQCR, IFC);
    int bck = FIELD_EX32(cpg->freqcr, FREQCR, BFC);
    int pck = FIELD_EX32(cpg->freqcr, FREQCR, PFC);
    const dev_clock_t *p = dev_clock_list;
    uint32_t divinput;

    divinput = cpg->xtal_freq_hz * k->pll1mul(cpg->clock_mode, cpg->freqcr);

    ick = ick < 4 ? ick + 1 : (ick - 1) * 2;
    clock_update_hz(cpg->clk_ick, divinput / ick);
    bck = bck < 4 ? bck + 1 : (bck - 1) * 2;
    clock_update_hz(cpg->clk_bck, divinput / bck);
    pck = pck < 3 ? pck + 2 : pck * 2;
    clock_update_hz(cpg->clk_pck, divinput / pck);

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

static void update_stbcr(SH7751CPGBaseState *cpg, int no, uint32_t diff)
{
    int bit = 0;
    const dev_clock_t *p;
    static const char *reg[] = {"STBCR", "STBCR2", "CLKSTP00"};

    while (diff) {
        if (diff & 1) {
            p = find_clock_list(no, bit);
            if (p) {
                set_clock_in(cpg, p);
            } else {
                qemu_log_mask(LOG_UNIMP, "sh7751-cpg: %s "
                              " bit %d is not implement.\n", reg[no], bit);
            }
        }
        bit++;
        diff >>= 1;
    }
}

static uint64_t cpg_read(void *opaque, hwaddr addr, unsigned size)
{
    SH7751CPGBaseState *cpg = SH7751CPGBase(opaque);
    int reg;

    switch (addr) {
    case A_FREQCR:
        if (size != 2) {
            qemu_log_mask(LOG_GUEST_ERROR, "sh7751-cpg: Register 0x%"
                          HWADDR_PRIX " Invalid access size.\n", addr);
            return UINT64_MAX;
        }
        return cpg->freqcr;
    case A_STBCR:
    case A_STBCR2:
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "sh7751-cpg: Register 0x%"
                          HWADDR_PRIX " Invalid access size.\n", addr);
            return UINT64_MAX;
        }
        reg = extract32(addr, 4, 1); /* STBCR -> 0x04 / STBCR2 -> 0x10 */
        return cpg->stbcr[reg];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "sh7751-cpg: Register 0x%"
                      HWADDR_PRIX " Invalid address.\n", addr);
        return UINT64_MAX;
    }

}

static void cpg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    SH7751CPGBaseState *cpg = SH7751CPGBase(opaque);
    uint32_t old_stbcr;
    int reg;

    switch (addr) {
    case A_FREQCR:
        if (size != 2) {
            qemu_log_mask(LOG_GUEST_ERROR, "sh7751-cpg: Register 0x%"
                          HWADDR_PRIX " Invalid access size.\n", addr);
            return;
        }
        if ((cpg->freqcr ^ val) & 0x0600) {
            qemu_log_mask(LOG_UNIMP,
                          "sh7751-cpg: PLL operation not supported.\n");
        }
        cpg->freqcr = val;
        update_divrate(cpg);
        break;
    case A_STBCR:
    case A_STBCR2:
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "sh7751-cpg: Register 0x%"
                          HWADDR_PRIX " Invalid access size.\n", addr);
            return;
        }
        reg = extract32(addr, 4, 1); /* STBCR -> 0x04 / STBCR2 -> 0x10 */
        old_stbcr = cpg->stbcr[reg];
        old_stbcr ^= val;
        cpg->stbcr[reg] = val;
        update_stbcr(cpg, reg, old_stbcr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "sh7751-cpg: Register 0x%"
                      HWADDR_PRIX " Invalid address.\n", addr);
    }
}

static uint64_t stp_read(void *opaque, hwaddr addr, unsigned size)
{
    SH7751CPGBaseState *cpg = SH7751CPGBase(opaque);

    switch (addr) {
    case A_CLKSTP00:
        return cpg->clkstp00;
    case A_CLKSTPCLR00:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "sh7751-cpg: CLKSTPCLR00 is write only.\n");
        return UINT64_MAX;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "sh7751-cpg: Register 0x%"
                      HWADDR_PRIX " Invalid address.\n", addr);
        return UINT64_MAX;
    }
}

static void stp_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    SH7751CPGBaseState *cpg = SH7751CPGBase(opaque);

    val &= 0x7;
    switch (addr) {
    case A_CLKSTP00:
        cpg->clkstp00 |= val;
        update_stbcr(cpg, 2, val);
        break;
    case A_CLKSTPCLR00:
        cpg->clkstp00 &= ~val;
        update_stbcr(cpg, 2, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "sh7751-cpg: Register 0x%"
                      HWADDR_PRIX " Invalid address.\n", addr);
    }
}

static int sh7751_pll1mul(int mode, uint16_t freqcr)
{
    int div1;
    int pll1;
    switch (mode) {
    case 3:
    case 5:
    case 6:
        div1 = 2;
        break;
    default:
        div1 = 1;
    }
    if (FIELD_EX16(freqcr, FREQCR, PLL1EN)) {
        pll1 = 6;
    } else {
        pll1 = 1;
    }
    return pll1 / div1;
}

static int sh7751r_pll1mul(int mode, uint16_t freqcr)
{
    int pll1;
    switch (mode) {
    case 0:
    case 1:
    case 3:
    case 5:
        pll1 = 12;
        break;
    case 2:
    case 4:
    case 6:
        pll1 = 6;
        break;
    default:
        g_assert_not_reached();
    }
    if (!FIELD_EX16(freqcr, FREQCR, PLL1EN)) {
        pll1 = 1;
    }
    return pll1;
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

static const MemoryRegionOps stp_ops = {
    .write = stp_write,
    .read  = stp_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const ClockPortInitArray sh7751_cpg_clocks = {
    QDEV_CLOCK_OUT(SH7751CPGBaseState, clk_ick),
    QDEV_CLOCK_OUT(SH7751CPGBaseState, clk_bck),
    QDEV_CLOCK_OUT(SH7751CPGBaseState, clk_pck),
    QDEV_CLOCK_END
};

static void sh7751cpg_realize(DeviceState *dev, Error **errp)
{
    SH7751CPGBaseState *cpg = SH7751CPGBase(dev);
    SH7751CPGBaseClass *k = SH7751CPG_GET_CLASS(cpg);

    if (cpg->xtal_freq_hz == 0) {
        error_setg(errp, "\"xtal-frequency-hz\" property must be provided.");
        return;
    }
    /* XTAL range: 1-34 MHz */
    if (cpg->xtal_freq_hz < SH7751_XTAL_MIN_HZ ||
        cpg->xtal_freq_hz > SH7751_XTAL_MAX_HZ) {
        error_setg(errp, "\"xtal-frequency-hz\" property in incorrect range.");
        return;
    }
    /* Clock mode: 0 - 6 */
    if (cpg->clock_mode > 6) {
        error_setg(errp, "\"clock-mode\" property in incorrect range.");
        return;
    }

    cpg->freqcr = k->initfreqcr[cpg->clock_mode];
    update_divrate(cpg);
}

static void sh7751_cpg_init(Object *obj)
{
    SH7751CPGBaseState *cpg = SH7751CPGBase(obj);
    const dev_clock_t *p = dev_clock_list;
    qdev_init_clocks(DEVICE(obj), sh7751_cpg_clocks);
    /* connect parent clock */
    while (p->name) {
        cpg->dev_clocks[p->devnum] = qdev_init_clock_out(DEVICE(obj),
                                                         p->name);
        p++;
    }

    memory_region_init_io(&cpg->memory[0], OBJECT(cpg), &cpg_ops,
                          cpg, "sh7751-cpg", 0x14);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &cpg->memory[0]);
    memory_region_init_alias(&cpg->memory[1], NULL,
                             "sh7751-cpg-a4", &cpg->memory[0], 0, 0x14);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &cpg->memory[1]);
    memory_region_init_alias(&cpg->memory[2], NULL,
                             "sh7751-cpg-p7", &cpg->memory[0], 0, 0x14);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &cpg->memory[2]);
    memory_region_init_io(&cpg->memory[3], OBJECT(cpg), &stp_ops,
                          cpg, "sh7751-stp", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &cpg->memory[3]);
    memory_region_init_alias(&cpg->memory[4], NULL,
                             "sh7751-stp-a4", &cpg->memory[3], 0, 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &cpg->memory[4]);
    memory_region_init_alias(&cpg->memory[5], NULL,
                             "sh7751-stp-p7", &cpg->memory[3], 0, 0x10u);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &cpg->memory[5]);
}

static Property sh7751_cpg_properties[] = {
    DEFINE_PROP_UINT32("xtal-frequency-hz",
                       SH7751CPGBaseState, xtal_freq_hz, 0),
    DEFINE_PROP_UINT32("clock-mode",
                       SH7751CPGBaseState, clock_mode, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void sh7751cpg_base_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sh7751_cpg_properties);
}

static void sh7751cpg_class_init(ObjectClass *klass, void *data)
{
    SH7751CPGBaseClass *base = SH7751CPGBaseClass(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    static uint16_t initfreqcr[] = {0x0e1a, 0x0e23, 0x0e13, 0x0e13,
                                   0x0e0a, 0x0e0a, 0x0808};

    base->pll1mul = sh7751_pll1mul;
    base->initfreqcr = initfreqcr;
    dc->realize = sh7751cpg_realize;
}

static void sh7751rcpg_class_init(ObjectClass *klass, void *data)
{
    SH7751CPGBaseClass *base = SH7751CPGBaseClass(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    static uint16_t initfreqcr[] = {0x0e1a, 0x0e2c, 0x0e13, 0x0e13,
                                   0x0e0a, 0x0e0a, 0x0808};

    base->pll1mul = sh7751r_pll1mul;
    base->initfreqcr = initfreqcr;
    dc->realize = sh7751cpg_realize;
}

static const TypeInfo sh7751cpg_info[] = {
    {
        .name       = TYPE_SH7751_CPG_BASE,
        .parent     = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(SH7751CPGBaseState),
        .class_init = sh7751cpg_base_class_init,
        .class_size = sizeof(SH7751CPGBaseClass),
        .abstract = true,
    },
    {
        .name       = TYPE_SH7751_CPG,
        .parent     = TYPE_SH7751_CPG_BASE,
        .instance_size = sizeof(SH7751CPGState),
        .instance_init = sh7751_cpg_init,
        .class_init = sh7751cpg_class_init,
        .class_size = sizeof(SH7751CPGClass),
    },
    {
        .name       = TYPE_SH7751R_CPG,
        .parent     = TYPE_SH7751_CPG_BASE,
        .instance_size = sizeof(SH7751RCPGState),
        .instance_init = sh7751_cpg_init,
        .class_init = sh7751rcpg_class_init,
        .class_size = sizeof(SH7751RCPGClass),
    },
};

DEFINE_TYPES(sh7751cpg_info)
