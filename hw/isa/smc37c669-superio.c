/*
 * SMC FDC37C669 Super I/O controller
 *
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This code is licensed under the GNU GPLv2 and later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Data Sheet (Rev. 06/29/2007):
 * http://ww1.microchip.com/downloads/en/DeviceDoc/37c669.pdf
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/isa/superio.h"
#include "trace.h"

#define SMC37C669(obj) \
            OBJECT_CHECK(SMC37C669State, (obj), TYPE_SMC37C669_SUPERIO)

typedef struct SMC37C669State {
    /*< private >*/
    ISASuperIODevice parent_dev;
    /*< public >*/

    uint32_t config; /* initial configuration */

    uint8_t cr[4];
} SMC37C669State;

/* UARTs (NS16C550 compatible) */

static bool is_serial_enabled(ISASuperIODevice *sio, uint8_t index)
{
    SMC37C669State *s = SMC37C669(sio);

    return extract32(s->cr[2], 3 + index * 4, 1);
}

static uint16_t get_serial_iobase(ISASuperIODevice *sio, uint8_t index)
{
    return index ? 0x2f8 : 0x3f8;
}

static unsigned int get_serial_irq(ISASuperIODevice *sio, uint8_t index)
{
    return index ? 3 : 4;
}

/* Parallel port (EPP and ECP support) */

static bool is_parallel_enabled(ISASuperIODevice *sio, uint8_t index)
{
    SMC37C669State *s = SMC37C669(sio);

    return extract32(s->cr[1], 2, 1);
}

static uint16_t get_parallel_iobase(ISASuperIODevice *sio, uint8_t index)
{
    return 0x3bc;
}

static unsigned int get_parallel_irq(ISASuperIODevice *sio, uint8_t index)
{
    return 7;
}

static unsigned int get_parallel_dma(ISASuperIODevice *sio, uint8_t index)
{
    return 3;
}

/* Diskette controller (Intel 82077 compatible) */

static bool is_fdc_enabled(ISASuperIODevice *sio, uint8_t index)
{
    SMC37C669State *s = SMC37C669(sio);

    return extract32(s->cr[0], 3, 1);
}

static uint16_t get_fdc_iobase(ISASuperIODevice *sio, uint8_t index)
{
    return 0x3f0;
}

static unsigned int get_fdc_irq(ISASuperIODevice *sio, uint8_t index)
{
    return 6;
}

static unsigned int get_fdc_dma(ISASuperIODevice *sio, uint8_t index)
{
    return 2;
}

static void smc37c669_reset(DeviceState *d)
{
    SMC37C669State *s = SMC37C669(d);

    stl_he_p(s->cr, s->config);
}

static void smc37c669_realize(DeviceState *dev, Error **errp)
{
    ISASuperIOClass *sc = ISA_SUPERIO_GET_CLASS(dev);
    Error *local_err = NULL;

    smc37c669_reset(dev);

    sc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static Property smc37c669_properties[] = {
    DEFINE_PROP_UINT32("config", SMC37C669State, config, 0x78889c28),
    DEFINE_PROP_BIT("parallel", SMC37C669State, config, 8 + 2, true),
    DEFINE_PROP_END_OF_LIST()
};

static void smc37c669_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISASuperIOClass *sc = ISA_SUPERIO_CLASS(klass);

    sc->parent_realize = dc->realize;
    dc->realize = smc37c669_realize;
    dc->reset = smc37c669_reset;
    dc->props = smc37c669_properties;

    sc->parallel = (ISASuperIOFuncs){
        .count = 1,
        .is_enabled = is_parallel_enabled,
        .get_iobase = get_parallel_iobase,
        .get_irq    = get_parallel_irq,
        .get_dma    = get_parallel_dma,
    };
    sc->serial = (ISASuperIOFuncs){
        .count = 2,
        .is_enabled = is_serial_enabled,
        .get_iobase = get_serial_iobase,
        .get_irq    = get_serial_irq,
    };
    sc->floppy = (ISASuperIOFuncs){
        .count = 1,
        .is_enabled = is_fdc_enabled,
        .get_iobase = get_fdc_iobase,
        .get_irq    = get_fdc_irq,
        .get_dma    = get_fdc_dma,
    };
}

static const TypeInfo smc37c669_type_info = {
    .name          = TYPE_SMC37C669_SUPERIO,
    .parent        = TYPE_ISA_SUPERIO,
    .instance_size = sizeof(SMC37C669State),
    .class_size    = sizeof(ISASuperIOClass),
    .class_init    = smc37c669_class_init,
};

static void smc37c669_register_types(void)
{
    type_register_static(&smc37c669_type_info);
}

type_init(smc37c669_register_types)
