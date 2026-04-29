/*
 * PIC32MK GPIO — PORTA through PORTG (7 ports)
 * Datasheet: DS60001519E, §12
 *
 * Each port is a SysBusDevice that exposes a PIC32MK_GPIO_PORT_SIZE (0x100)
 * byte MMIO region containing ANSEL, TRIS, PORT, LAT, ODC, CN* registers.
 *
 * All registers support SET/CLR/INV sub-registers (+4/+8/+C).
 *
 * External input model:
 *   - 16 qdev GPIO input lines per port — driven by board wiring or test code.
 *   - 16 qdev GPIO output lines per port — assert when an output pin's LAT changes.
 *   - One IRQ output line → EVIC CN interrupt (fires when CNCON.ON and CNEN0
 *     enabled bits detect a PORT change).
 *
 * QOM properties (accessible via qom-get / qom-set from HMP/QMP):
 *   pin0..pin15 - bool, r/w - inject external input (write) or read PORT bit
 *   lat-state     — uint32, r  — current LAT register value
 *   port-state    — uint32, r  — current PORT register value (mix of ext & output)
 *   tris-state    — uint32, r  — current TRIS register value
 *
 * Chardev event stream (optional "chardev" qdev property):
 *   When connected, each state change emits a 13-byte little-endian message:
 *     [port_idx:1] [tris:4] [lat:4] [port:4]
 *   Enables event-driven host-side GPIO monitoring (gpio_tool.py gui).
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "chardev/char.h"
#include "hw/mips/pic32mk.h"
#include "qom/object.h"

/*
 * Device state — one instance per port (A–G)
 * -----------------------------------------------------------------------
 */

#define TYPE_PIC32MK_GPIO   "pic32mk-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKGpioState, PIC32MK_GPIO)

struct PIC32MKGpioState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    /* SFR registers */
    uint32_t ansel;
    uint32_t tris;    /* 1 = input (reset default) */
    uint32_t port;    /* pin state: input bits from ext_input, output bits from lat */
    uint32_t lat;     /* output latch */
    uint32_t odc;
    uint32_t cnpu;
    uint32_t cnpd;
    uint32_t cncon;
    uint32_t cnen0;
    uint32_t cnstat;
    uint32_t cnen1;
    uint32_t cnf;

    /* External input pin levels (driven by qdev GPIO input lines or QOM set) */
    uint32_t ext_input;

    /* CN reference: last PORT value read by firmware — used for mismatch detect */
    uint32_t cn_ref;

    /* IRQ output → EVIC CN interrupt */
    qemu_irq cn_irq;

    /* qdev GPIO output lines (16 per port): asserted when output LAT changes */
    qemu_irq output[16];

    /*
     * Optional shared chardev for streaming state-change events to host tools.
     * Multiple GPIO ports write to the same Chardev* (not CharFrontend,
     * which enforces 1:1 ownership). Set by board init if chardev exists.
      */
    Chardev *chr_out;
    uint8_t  port_idx;   /* 0=A, 1=B, …, 6=G */
};

/*
 * MMIO helpers
 * -----------------------------------------------------------------------
 */

/* PIC32MK: base+0=REG, +4=CLR, +8=SET, +0xC=INV */
static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        *reg  = val;
        break;
    case 4:
        *reg &= ~val;
        break;
    case 8:
        *reg |= val;
        break;
    case 12:
        *reg ^= val;
        break;
    }
}

static uint32_t *gpio_find_reg(PIC32MKGpioState *s, hwaddr base)
{
    switch (base) {
    case PIC32MK_ANSEL:
        return &s->ansel;
    case PIC32MK_TRIS:
        return &s->tris;
    case PIC32MK_PORT:
        return &s->port;
    case PIC32MK_LAT:
        return &s->lat;
    case PIC32MK_ODC:
        return &s->odc;
    case PIC32MK_CNPU:
        return &s->cnpu;
    case PIC32MK_CNPD:
        return &s->cnpd;
    case PIC32MK_CNCON:
        return &s->cncon;
    case PIC32MK_CNEN0:
        return &s->cnen0;
    case PIC32MK_CNSTAT:
        return &s->cnstat;
    case PIC32MK_CNEN1:
        return &s->cnen1;
    case PIC32MK_CNF:
        return &s->cnf;
    default:
        return NULL;
    }
}

/* Recompute s->port from current lat, tris, and ext_input. */
static void gpio_update_port(PIC32MKGpioState *s)
{
    /* Input pins reflect external levels; output pins reflect LAT. */
    s->port = (s->ext_input & s->tris) | (s->lat & ~s->tris);
}

/*
 * Emit a 13-byte state-change event on the optional chardev.
 * Format (little-endian): [port_idx:1] [tris:4] [lat:4] [port:4]
 */
static void gpio_notify_state(PIC32MKGpioState *s)
{
    if (!s->chr_out) {
        return;
    }
    uint8_t msg[13];
    msg[0] = s->port_idx;
    memcpy(&msg[1],  &s->tris, 4);
    memcpy(&msg[5],  &s->lat,  4);
    memcpy(&msg[9],  &s->port, 4);
    qemu_chr_write_all(s->chr_out, msg, sizeof(msg));
}

/* Drive the 16 qdev output GPIO lines to match current LAT (output pins only). */
static void gpio_drive_outputs(PIC32MKGpioState *s)
{
    uint32_t out = s->lat & ~s->tris;
    for (int i = 0; i < 16; i++) {
        qemu_set_irq(s->output[i], (out >> i) & 1);
    }
}

/*
 * Evaluate CN interrupt.
 *
 * PIC32MK CN is mismatch-based: CNSTAT bits are set when the current PORT
 * value differs from the value captured at the last PORT read (cn_ref).
 * The interrupt line is held HIGH as long as any enabled mismatch exists;
 * reading PORT (in the ISR) updates cn_ref and clears the mismatch, which
 * deasserts the interrupt so the EVIC doesn't immediately re-set IFS.
 */
static void gpio_eval_cn(PIC32MKGpioState *s)
{
#define CNCON_ON  (1u << 15)
    if (!(s->cncon & CNCON_ON)) {
        return;
    }
    /* Mismatch between current port and the reference snapshot */
    uint32_t mismatch = (s->port ^ s->cn_ref) & s->cnen0;
    s->cnstat = mismatch;

    if (mismatch) {
        qemu_set_irq(s->cn_irq, 1);   /* assert — EVIC sets IFS */
    } else {
        qemu_set_irq(s->cn_irq, 0);   /* deassert — no more mismatch */
    }
}

/*
 * qdev GPIO input handler (external pin injection)
 * -----------------------------------------------------------------------
 */

static void gpio_set_input(PIC32MKGpioState *s, int pin, int level)
{
    uint32_t mask = 1u << pin;

    if (level) {
        s->ext_input |= mask;
    } else {
        s->ext_input &= ~mask;
    }

    gpio_update_port(s);
    gpio_eval_cn(s);
    gpio_notify_state(s);
}

static void gpio_set_input_line(void *opaque, int pin, int level)
{
    gpio_set_input(PIC32MK_GPIO(opaque), pin, level);
}

/*
 * MMIO read/write
 * -----------------------------------------------------------------------
 */

static uint64_t gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKGpioState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;
    uint32_t *reg = gpio_find_reg(s, base);

    if (reg) {
        /*
         * Reading PORT captures the current pin state into cn_ref.
         * This clears the CN mismatch and deasserts the interrupt line,
         * just like real PIC32MK hardware.
         */
        if (base == PIC32MK_PORT) {
            s->cn_ref = s->port;
            gpio_eval_cn(s);    /* deassert if no longer mismatching */
        }
        return *reg;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_gpio: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

static void gpio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKGpioState *s = opaque;
    int sub       = (int)(addr & 0xF);
    hwaddr base   = addr & ~(hwaddr)0xF;
    uint32_t *reg = gpio_find_reg(s, base);

    if (!reg) {
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_gpio: unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      addr, val);
        return;
    }

    apply_sci(reg, (uint32_t)val, sub);

    /* Side-effects after write */
    if (base == PIC32MK_LAT || base == PIC32MK_TRIS) {
        /* Output pin levels or direction changed — update PORT and drive lines */
        gpio_update_port(s);
        gpio_drive_outputs(s);
        gpio_eval_cn(s);
        gpio_notify_state(s);
    } else if (base == PIC32MK_ANSEL) {
        gpio_notify_state(s);
    }
    if (base == PIC32MK_CNCON || base == PIC32MK_CNEN0 || base == PIC32MK_CNEN1) {
        gpio_eval_cn(s);
    }
}

static const MemoryRegionOps gpio_ops = {
    .read       = gpio_read,
    .write      = gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * QOM properties — host access via qom-get / qom-set
 * -----------------------------------------------------------------------
 */

static void gpio_prop_pin_get(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    PIC32MKGpioState *s = PIC32MK_GPIO(obj);
    int pin = (int)(intptr_t)opaque;
    bool val = (s->port >> pin) & 1;
    visit_type_bool(v, name, &val, errp);
}

static void gpio_prop_pin_set(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    PIC32MKGpioState *s = PIC32MK_GPIO(obj);
    int pin = (int)(intptr_t)opaque;
    bool val;

    if (!visit_type_bool(v, name, &val, errp)) {
        return;
    }
    gpio_set_input(s, pin, val ? 1 : 0);
}

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_gpio_reset(DeviceState *dev)
{
    PIC32MKGpioState *s = PIC32MK_GPIO(dev);

    s->ansel     = 0xFFFF;   /* all pins analog on reset */
    s->tris      = 0xFFFF;   /* all pins input on reset */
    s->port      = 0;
    s->lat       = 0;
    s->odc       = 0;
    s->cnpu      = 0;
    s->cnpd      = 0;
    s->cncon     = 0;
    s->cnen0     = 0;
    s->cnstat    = 0;
    s->cnen1     = 0;
    s->cnf       = 0;
    s->ext_input = 0;
    s->cn_ref    = 0;
}

static void pic32mk_gpio_init(Object *obj)
{
    PIC32MKGpioState *s = PIC32MK_GPIO(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mr, obj, &gpio_ops, s,
                          TYPE_PIC32MK_GPIO, PIC32MK_GPIO_PORT_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    /* IRQ output → EVIC CN interrupt */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->cn_irq);

    /* 16 qdev GPIO input lines: board/test code drives external pin levels */
    qdev_init_gpio_in(dev, gpio_set_input_line, 16);

    /* 16 qdev GPIO output lines: driven when LAT output pins change */
    qdev_init_gpio_out(dev, s->output, 16);

    /* Per-pin bool QOM properties: pin0 … pin15 */
    for (int i = 0; i < 16; i++) {
        char name[8];
        snprintf(name, sizeof(name), "pin%d", i);
        object_property_add(obj, name, "bool",
                            gpio_prop_pin_get,
                            gpio_prop_pin_set,
                            NULL, (void *)(intptr_t)i);
    }

    /* Whole-port read-only convenience properties */
    object_property_add_uint32_ptr(obj, "lat-state",  &s->lat,  OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "port-state", &s->port, OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "tris-state", &s->tris, OBJ_PROP_FLAG_READ);
}

void pic32mk_gpio_set_chardev(DeviceState *dev, Chardev *chr)
{
    PIC32MKGpioState *s = PIC32MK_GPIO(dev);
    s->chr_out = chr;
}

static const Property pic32mk_gpio_props[] = {
    DEFINE_PROP_UINT8("port-index", PIC32MKGpioState, port_idx, 0),
};

static void pic32mk_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_gpio_reset);
    device_class_set_props(dc, pic32mk_gpio_props);
}

static const TypeInfo pic32mk_gpio_info = {
    .name          = TYPE_PIC32MK_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKGpioState),
    .instance_init = pic32mk_gpio_init,
    .class_init    = pic32mk_gpio_class_init,
};

static void pic32mk_gpio_register_types(void)
{
    type_register_static(&pic32mk_gpio_info);
}

type_init(pic32mk_gpio_register_types)
