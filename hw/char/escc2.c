/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Enhanced Serial Communication Controller (ESCC2 v3.2).
 * Modelled according to the user manual (version 07.96).
 *
 * Copyright (C) 2020 Jasper Lowell
 */

#include "qemu/osdep.h"
#include "hw/char/escc2.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "sysemu/reset.h"
#include "trace.h"

/* STAR. */
#define REGISTER_STAR_OFFSET                    0x20

/* CMDR. */
#define REGISTER_CMDR_OFFSET                    0x20

/* MODE. */
#define REGISTER_MODE_OFFSET                    0x22

/* TIMR. */
#define REGISTER_TIMR_OFFSET                    0x23

/* XON. */
#define REGISTER_XON_OFFSET                     0x24

/* XOFF. */
#define REGISTER_XOFF_OFFSET                    0x25

/* TCR. */
#define REGISTER_TCR_OFFSET                     0x26

/* DAFO. */
#define REGISTER_DAFO_OFFSET                    0x27

/* RFC. */
#define REGISTER_RFC_OFFSET                     0x28

/* RBCL. */
#define REGISTER_RBCL_OFFSET                    0x2a

/* XBCL. */
#define REGISTER_XBCL_OFFSET                    0x2a

/* RBCH. */
#define REGISTER_RBCH_OFFSET                    0x2b

/* XBCH. */
#define REGISTER_XBCH_OFFSET                    0x2b

/* CCR0. */
#define REGISTER_CCR0_OFFSET                    0x2c
#define REGISTER_CCR0_PU                        0x80
#define REGISTER_CCR0_MCE                       0x40
#define REGISTER_CCR0_SC2                       0x10
#define REGISTER_CCR0_SC1                       0x8
#define REGISTER_CCR0_SC0                       0x4
#define REGISTER_CCR0_SM1                       0x2
#define REGISTER_CCR0_SM0                       0x1

/* CCR1. */
#define REGISTER_CCR1_OFFSET                    0x2d

/* CCR2. */
#define REGISTER_CCR2_OFFSET                    0x2e

/* CCR3. */
#define REGISTER_CCR3_OFFSET                    0x2f

/* TSAX. */
#define REGISTER_TSAX_OFFSET                    0x30

/* TSAR. */
#define REGISTER_TSAR_OFFSET                    0x31

/* XCCR. */
#define REGISTER_XCCR_OFFSET                    0x32

/* RCCR. */
#define REGISTER_RCCR_OFFSET                    0x33

/* VSTR. */
#define REGISTER_VSTR_OFFSET                    0x34

/* BGR. */
#define REGISTER_BGR_OFFSET                     0x34

/* TIC. */
#define REGISTER_TIC_OFFSET                     0x35

/* MXN. */
#define REGISTER_MXN_OFFSET                     0x36

/* MXF. */
#define REGISTER_MXF_OFFSET                     0x37

/* GIS. */
#define REGISTER_GIS_OFFSET                     0x38
#define REGISTER_GIS_PI                         0x80
#define REGISTER_GIS_ISA1                       0x8
#define REGISTER_GIS_ISA0                       0x4
#define REGISTER_GIS_ISB1                       0x2
#define REGISTER_GIS_ISB0                       0x1

/* IVA. */
#define REGISTER_IVA_OFFSET                     0x38

/* IPC. */
#define REGISTER_IPC_OFFSET                     0x39
#define REGISTER_IPC_VIS                        0x80
#define REGISTER_IPC_SLA1                       0x10
#define REGISTER_IPC_SLA0                       0x8
#define REGISTER_IPC_CASM                       0x4
#define REGISTER_IPC_IC1                        0x2
#define REGISTER_IPC_IC0                        0x1

/* ISR0. */
#define REGISTER_ISR0_OFFSET                    0x3a

/* IMR0. */
#define REGISTER_IMR0_OFFSET                    0x3a

/* ISR1. */
#define REGISTER_ISR1_OFFSET                    0x3b

/* IMR1. */
#define REGISTER_IMR1_OFFSET                    0x3b

/* PVR. */
#define REGISTER_PVR_OFFSET                     0x3c

/* PIS. */
#define REGISTER_PIS_OFFSET                     0x3d

/* PIM. */
#define REGISTER_PIM_OFFSET                     0x3d

/* PCR. */
#define REGISTER_PCR_OFFSET                     0x3e

/* CCR4. */
#define REGISTER_CCR4_OFFSET                    0x3f

enum {
    REGISTER_STAR = 0,
    REGISTER_CMDR,
    REGISTER_MODE,
    REGISTER_TIMR,
    REGISTER_XON,
    REGISTER_XOFF,
    REGISTER_TCR,
    REGISTER_DAFO,
    REGISTER_RFC,
    REGISTER_RBCL,
    REGISTER_XBCL,
    REGISTER_RBCH,
    REGISTER_XBCH,
    REGISTER_CCR0,
    REGISTER_CCR1,
    REGISTER_CCR2,
    REGISTER_CCR3,
    REGISTER_TSAX,
    REGISTER_TSAR,
    REGISTER_XCCR,
    REGISTER_RCCR,
    REGISTER_VSTR,
    REGISTER_BGR,
    REGISTER_TIC,
    REGISTER_MXN,
    REGISTER_MXF,
    REGISTER_GIS,
    REGISTER_IVA,
    REGISTER_IPC,
    REGISTER_ISR0,
    REGISTER_IMR0,
    REGISTER_ISR1,
    REGISTER_IMR1,
    REGISTER_PVR,
    REGISTER_PIS,
    REGISTER_PIM,
    REGISTER_PCR,
    REGISTER_CCR4,
    /* End. */
    REGISTER_COUNT
};

typedef struct ESCC2State ESCC2State;

#define CHANNEL_FIFO_LENGTH                     0x20
typedef struct ESCC2ChannelState {
    ESCC2State *controller;

    /*
     * The SAB 82532 ships with 64 byte FIFO queues for transmitting and
     * receiving but only 32 bytes are addressable.
     */
    uint8_t fifo_receive[CHANNEL_FIFO_LENGTH];
    uint8_t fifo_transmit[CHANNEL_FIFO_LENGTH];

    uint8_t register_set[REGISTER_COUNT];
} ESCC2ChannelState;

#define CHANNEL_A_OFFSET                        0x0
#define CHANNEL_B_OFFSET                        0x40
#define CHANNEL_LENGTH                          0x40

#define REGISTER_READ(channel, idx) \
    ((channel)->register_set[(idx)])
#define REGISTER_WRITE(channel, idx, value) \
    ((channel)->register_set[(idx)] = (value))

enum {
    CHANNEL_A = 0,
    CHANNEL_B,
    /* End. */
    CHANNEL_COUNT
};

struct ESCC2State {
    DeviceState parent;

    MemoryRegion io;
    qemu_irq irq;
    ESCC2ChannelState channel[CHANNEL_COUNT];
};

#define CONTROLLER_CHANNEL_A(controller) (&(controller)->channel[CHANNEL_A])
#define CONTROLLER_CHANNEL_B(controller) (&(controller)->channel[CHANNEL_B])
#define CHANNEL_CHAR(channel) \
    ((channel) == CONTROLLER_CHANNEL_A((channel)->controller) ? 'A' : 'B')

typedef struct ESCC2ISAState {
    ISADevice parent;
    uint32_t iobase;
    uint32_t irq;
    struct ESCC2State controller;
} ESCC2ISAState;

static void escc2_irq_update(ESCC2State *controller)
{
    bool power;
    uint8_t gis;
    ESCC2ChannelState *a, *b;

    gis = 0;
    a = CONTROLLER_CHANNEL_A(controller);
    b = CONTROLLER_CHANNEL_B(controller);

    /*
     * Interrupts are not propagated to the CPU when in power-down mode. There
     * is an exception for interrupts from the universal port.
     */
    power = REGISTER_READ(a, REGISTER_CCR0) & REGISTER_CCR0_PU;

    if (REGISTER_READ(a, REGISTER_ISR0) & ~(REGISTER_READ(a, REGISTER_IMR0))) {
        gis |= REGISTER_GIS_ISA0;
    }
    if (REGISTER_READ(a, REGISTER_ISR1) & ~(REGISTER_READ(a, REGISTER_IMR1))) {
        gis |= REGISTER_GIS_ISA1;
    }

    if (REGISTER_READ(b, REGISTER_ISR0) & ~(REGISTER_READ(b, REGISTER_IMR0))) {
        gis |= REGISTER_GIS_ISB0;
    }
    if (REGISTER_READ(b, REGISTER_ISR1) & ~(REGISTER_READ(b, REGISTER_IMR1))) {
        gis |= REGISTER_GIS_ISB1;
    }

    if (REGISTER_READ(a, REGISTER_PIS) & ~(REGISTER_READ(a, REGISTER_PIM))) {
        gis |= REGISTER_GIS_PI;
        /*
         * Ensure that interrupts are propagated even if the controller is in
         * power-down mode.
         */
        power = true;
    }

    /* GIS is accessible from either channel and must be synchronised. */
    REGISTER_WRITE(a, REGISTER_GIS, gis);
    REGISTER_WRITE(b, REGISTER_GIS, gis);

    if (gis && power) {
        qemu_irq_raise(controller->irq);
    } else {
        qemu_irq_lower(controller->irq);
    }

    trace_escc2_irq_update(gis);
}

static void escc2_channel_reset(ESCC2ChannelState *channel)
{
    unsigned int i;

    memset(channel->fifo_receive, 0, sizeof(channel->fifo_receive));
    memset(channel->fifo_transmit, 0, sizeof(channel->fifo_transmit));
    for (i = 0; i < REGISTER_COUNT; i++) {
        channel->register_set[i] = 0;
    }

    channel->register_set[REGISTER_STAR] = 0x40;
    channel->register_set[REGISTER_VSTR] = 0x2;
}

static void escc2_reset(void *opaque)
{
    unsigned int i;
    ESCC2State *controller = opaque;

    for (i = 0; i < CHANNEL_COUNT; i++) {
        escc2_channel_reset(&controller->channel[i]);
    }
}

static uint64_t escc2_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t value, offset;
    ESCC2State *controller;
    ESCC2ChannelState *channel;

    assert(addr < (CHANNEL_COUNT * CHANNEL_LENGTH));
    assert(size == sizeof(uint8_t));

    controller = opaque;
    if (addr < CHANNEL_LENGTH) {
        channel = CONTROLLER_CHANNEL_A(controller);
        offset = addr;
    } else {
        channel = CONTROLLER_CHANNEL_B(controller);
        offset = addr - CHANNEL_LENGTH;
    }

    switch (offset) {
    case 0 ... (CHANNEL_FIFO_LENGTH - 1):
        value = channel->fifo_receive[offset];
        break;
    case REGISTER_STAR_OFFSET:
        value = REGISTER_READ(channel, REGISTER_STAR);
        break;
    case REGISTER_MODE_OFFSET:
        value = REGISTER_READ(channel, REGISTER_MODE);
        break;
    case REGISTER_TIMR_OFFSET:
        value = REGISTER_READ(channel, REGISTER_TIMR);
        break;
    case REGISTER_XON_OFFSET:
        value = REGISTER_READ(channel, REGISTER_XON);
        break;
    case REGISTER_XOFF_OFFSET:
        value = REGISTER_READ(channel, REGISTER_XOFF);
        break;
    case REGISTER_TCR_OFFSET:
        value = REGISTER_READ(channel, REGISTER_TCR);
        break;
    case REGISTER_DAFO_OFFSET:
        value = REGISTER_READ(channel, REGISTER_DAFO);
        break;
    case REGISTER_RFC_OFFSET:
        value = REGISTER_READ(channel, REGISTER_RFC);
        break;
    case REGISTER_RBCL_OFFSET:
        value = REGISTER_READ(channel, REGISTER_RBCL);
        break;
    case REGISTER_RBCH_OFFSET:
        value = REGISTER_READ(channel, REGISTER_RBCH);
        break;
    case REGISTER_CCR0_OFFSET:
        value = REGISTER_READ(channel, REGISTER_CCR0);
        break;
    case REGISTER_CCR1_OFFSET:
        value = REGISTER_READ(channel, REGISTER_CCR1);
        break;
    case REGISTER_CCR2_OFFSET:
        value = REGISTER_READ(channel, REGISTER_CCR2);
        break;
    case REGISTER_CCR3_OFFSET:
        value = REGISTER_READ(channel, REGISTER_CCR3);
        break;
    case REGISTER_VSTR_OFFSET:
        value = REGISTER_READ(channel, REGISTER_VSTR);
        break;
    case REGISTER_GIS_OFFSET:
        value = REGISTER_READ(channel, REGISTER_GIS);
        break;
    case REGISTER_IPC_OFFSET:
        value = REGISTER_READ(channel, REGISTER_IPC);
        break;
    case REGISTER_ISR0_OFFSET:
        value = REGISTER_READ(channel, REGISTER_ISR0);
        REGISTER_WRITE(channel, REGISTER_ISR0, 0);
        escc2_irq_update(controller);
        break;
    case REGISTER_ISR1_OFFSET:
        value = REGISTER_READ(channel, REGISTER_ISR1);
        REGISTER_WRITE(channel, REGISTER_ISR1, 0);
        escc2_irq_update(controller);
        break;
    case REGISTER_PVR_OFFSET:
        value = REGISTER_READ(channel, REGISTER_PVR);
        break;
    case REGISTER_PIS_OFFSET:
        value = REGISTER_READ(channel, REGISTER_PIS);
        REGISTER_WRITE(CONTROLLER_CHANNEL_A(controller), REGISTER_PIS, 0);
        REGISTER_WRITE(CONTROLLER_CHANNEL_B(controller), REGISTER_PIS, 0);
        escc2_irq_update(controller);
        break;
    case REGISTER_PCR_OFFSET:
        value = REGISTER_READ(channel, REGISTER_PCR);
        break;
    case REGISTER_CCR4_OFFSET:
        value = REGISTER_READ(channel, REGISTER_CCR4);
        break;
    default:
        value = 0;
        break;
    }

    trace_escc2_mem_read(CHANNEL_CHAR(channel), offset, value);
    return value;
}

static void escc2_mem_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    uint8_t offset;
    ESCC2State *controller;
    ESCC2ChannelState *channel;

    assert(addr < (CHANNEL_COUNT * CHANNEL_LENGTH));
    assert(size == sizeof(uint8_t));
    assert(value <= 0xff);

    controller = opaque;
    if (addr < CHANNEL_LENGTH) {
        channel = CONTROLLER_CHANNEL_A(controller);
        offset = addr;
    } else {
        channel = CONTROLLER_CHANNEL_B(controller);
        offset = addr - CHANNEL_LENGTH;
    }

    switch (offset) {
    case 0 ... (CHANNEL_FIFO_LENGTH - 1):
        channel->fifo_transmit[offset] = value;
        break;
    case REGISTER_CMDR_OFFSET:
        REGISTER_WRITE(channel, REGISTER_CMDR, value);
        break;
    case REGISTER_MODE_OFFSET:
        REGISTER_WRITE(channel, REGISTER_MODE, value);
        break;
    case REGISTER_TIMR_OFFSET:
        REGISTER_WRITE(channel, REGISTER_TIMR, value);
        break;
    case REGISTER_XON_OFFSET:
        REGISTER_WRITE(channel, REGISTER_XON, value);
        break;
    case REGISTER_XOFF_OFFSET:
        REGISTER_WRITE(channel, REGISTER_XOFF, value);
        break;
    case REGISTER_TCR_OFFSET:
        REGISTER_WRITE(channel, REGISTER_TCR, value);
        break;
    case REGISTER_DAFO_OFFSET:
        REGISTER_WRITE(channel, REGISTER_DAFO, value);
        break;
    case REGISTER_RFC_OFFSET:
        REGISTER_WRITE(channel, REGISTER_RFC, value);
        break;
    case REGISTER_XBCL_OFFSET:
        REGISTER_WRITE(channel, REGISTER_XBCL, value);
        break;
    case REGISTER_XBCH_OFFSET:
        REGISTER_WRITE(channel, REGISTER_XBCH, value);
        break;
    case REGISTER_CCR0_OFFSET:
        REGISTER_WRITE(channel, REGISTER_CCR0, value);
        break;
    case REGISTER_CCR1_OFFSET:
        REGISTER_WRITE(channel, REGISTER_CCR1, value);
        break;
    case REGISTER_CCR2_OFFSET:
        REGISTER_WRITE(channel, REGISTER_CCR2, value);
        break;
    case REGISTER_CCR3_OFFSET:
        REGISTER_WRITE(channel, REGISTER_CCR3, value);
        break;
    case REGISTER_TSAX_OFFSET:
        REGISTER_WRITE(channel, REGISTER_TSAX, value);
        break;
    case REGISTER_TSAR_OFFSET:
        REGISTER_WRITE(channel, REGISTER_TSAR, value);
        break;
    case REGISTER_XCCR_OFFSET:
        REGISTER_WRITE(channel, REGISTER_XCCR, value);
        break;
    case REGISTER_RCCR_OFFSET:
        REGISTER_WRITE(channel, REGISTER_RCCR, value);
        break;
    case REGISTER_BGR_OFFSET:
        REGISTER_WRITE(channel, REGISTER_BGR, value);
        break;
    case REGISTER_TIC_OFFSET:
        REGISTER_WRITE(channel, REGISTER_TIC, value);
        break;
    case REGISTER_MXN_OFFSET:
        REGISTER_WRITE(channel, REGISTER_MXN, value);
        break;
    case REGISTER_MXF_OFFSET:
        REGISTER_WRITE(channel, REGISTER_MXF, value);
        break;
    case REGISTER_IVA_OFFSET:
        REGISTER_WRITE(CONTROLLER_CHANNEL_A(controller), REGISTER_IVA, value);
        REGISTER_WRITE(CONTROLLER_CHANNEL_B(controller), REGISTER_IVA, value);
        break;
    case REGISTER_IPC_OFFSET:
        REGISTER_WRITE(CONTROLLER_CHANNEL_A(controller), REGISTER_IPC, value);
        REGISTER_WRITE(CONTROLLER_CHANNEL_B(controller), REGISTER_IPC, value);
        break;
    case REGISTER_IMR0_OFFSET:
        REGISTER_WRITE(channel, REGISTER_IMR0, value);
        break;
    case REGISTER_IMR1_OFFSET:
        REGISTER_WRITE(channel, REGISTER_IMR1, value);
        break;
    case REGISTER_PVR_OFFSET:
        REGISTER_WRITE(channel, REGISTER_PVR, value);
        break;
    case REGISTER_PIM_OFFSET:
        REGISTER_WRITE(CONTROLLER_CHANNEL_A(controller), REGISTER_PIM, value);
        REGISTER_WRITE(CONTROLLER_CHANNEL_B(controller), REGISTER_PIM, value);
        break;
    case REGISTER_PCR_OFFSET:
        REGISTER_WRITE(CONTROLLER_CHANNEL_A(controller), REGISTER_PCR, value);
        REGISTER_WRITE(CONTROLLER_CHANNEL_B(controller), REGISTER_PCR, value);
        break;
    case REGISTER_CCR4_OFFSET:
        REGISTER_WRITE(channel, REGISTER_CCR4, value);
        break;
    default:
        /* Registers do not exhaustively cover the addressable region. */
        break;
    }

    trace_escc2_mem_write(CHANNEL_CHAR(channel), offset, value);
}

static void escc2_realize(DeviceState *dev, Error **errp)
{
    unsigned int i;
    ESCC2ChannelState *channel;
    ESCC2State *controller = ESCC2(dev);

    for (i = 0; i < CHANNEL_COUNT; i++) {
        channel = &controller->channel[i];
        channel->controller = controller;
    }

    qemu_register_reset(escc2_reset, controller);
    escc2_reset(controller);
}

const MemoryRegionOps escc2_mem_ops = {
    .read = escc2_mem_read,
    .write = escc2_mem_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void escc2_isa_realize(DeviceState *dev, Error **errp)
{
    ESCC2ISAState *isa = ESCC2_ISA(dev);
    ESCC2State *controller = &isa->controller;

    if (isa->iobase == -1) {
        error_setg(errp, "Base address must be provided.");
        return;
    }

    if (isa->irq == -1) {
        error_setg(errp, "IRQ must be provided.");
        return;
    }

    isa_init_irq(ISA_DEVICE(dev), &controller->irq, isa->irq);

    object_property_set_bool(OBJECT(controller), true, "realized", errp);
    if (*errp) {
        return;
    }

    memory_region_init_io(&controller->io, OBJECT(dev), &escc2_mem_ops,
            controller, "escc2", CHANNEL_COUNT * CHANNEL_LENGTH);
    isa_register_ioport(ISA_DEVICE(dev), &controller->io, isa->iobase);
}

static void escc2_unrealize(DeviceState *dev)
{
    ESCC2State *controller = ESCC2(dev);
    qemu_unregister_reset(escc2_reset, controller);
}

static void escc2_isa_instance_init(Object *o)
{
    ESCC2ISAState *self = ESCC2_ISA(o);
    object_initialize_child(o, "escc2", &self->controller,
            sizeof(self->controller), TYPE_ESCC2, &error_abort, NULL);
    qdev_alias_all_properties(DEVICE(&self->controller), o);
}

static Property escc2_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

static Property escc2_isa_properties[] = {
    DEFINE_PROP_UINT32("iobase", ESCC2ISAState, iobase, -1),
    DEFINE_PROP_UINT32("irq", ESCC2ISAState, irq, -1),
    DEFINE_PROP_END_OF_LIST()
};

static void escc2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->user_creatable = false;
    dc->realize = escc2_realize;
    dc->unrealize = escc2_unrealize;
    device_class_set_props(dc, escc2_properties);
}

static void escc2_isa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, escc2_isa_properties);
    dc->realize = escc2_isa_realize;
}

static const TypeInfo escc2_info = {
    .name = TYPE_ESCC2,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(ESCC2State),
    .class_init = escc2_class_init
};

static const TypeInfo escc2_isa_info = {
    .name = TYPE_ESCC2_ISA,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ESCC2ISAState),
    .instance_init = escc2_isa_instance_init,
    .class_init = escc2_isa_class_init
};

static void escc2_types(void)
{
    type_register_static(&escc2_info);
    type_register_static(&escc2_isa_info);
}

type_init(escc2_types);
