/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Enhanced Serial Communication Controller (ESCC2 v3.2).
 * Modelled according to the user manual (version 07.96).
 *
 * Copyright (C) 2020 Jasper Lowell
 */

#include "qemu/osdep.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "hw/char/escc2.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "sysemu/reset.h"
#include "trace.h"

/* STAR. */
#define REGISTER_STAR_OFFSET                    0x20
#define REGISTER_STAR_XDOV                      0x80
#define REGISTER_STAR_XFW                       0x40
#define REGISTER_STAR_RFNE                      0x20
#define REGISTER_STAR_FCS                       0x10
#define REGISTER_STAR_TEC                       0x8
#define REGISTER_STAR_CEC                       0x4
#define REGISTER_STAR_CTS                       0x2

/* CMDR. */
#define REGISTER_CMDR_OFFSET                    0x20
#define REGISTER_CMDR_RMC                       0x80
#define REGISTER_CMDR_RRES                      0x40
#define REGISTER_CMDR_RFRD                      0x20
#define REGISTER_CMDR_STI                       0x10
#define REGISTER_CMDR_XF                        0x8
#define REGISTER_CMDR_XRES                      0x1

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
#define REGISTER_DAFO_XBRK                      0x40
#define REGISTER_DAFO_STOP                      0x20
#define REGISTER_DAFO_PAR1                      0x10
#define REGISTER_DAFO_PAR0                      0x8
#define REGISTER_DAFO_PARE                      0x4
#define REGISTER_DAFO_CHL1                      0x2
#define REGISTER_DAFO_CHL0                      0x1

#define REGISTER_DAFO_PAR_MASK \
    (REGISTER_DAFO_PAR1 | REGISTER_DAFO_PAR0)
#define REGISTER_DAFO_PAR_SPACE                 0x0
#define REGISTER_DAFO_PAR_ODD                   (REGISTER_DAFO_PAR0)
#define REGISTER_DAFO_PAR_EVEN                  (REGISTER_DAFO_PAR1)
#define REGISTER_DAFO_PAR_MARK \
    (REGISTER_DAFO_PAR1 | REGISTER_DAFO_PAR0)
#define REGISTER_DAFO_CHL_MASK \
    (REGISTER_DAFO_CHL1 | REGISTER_DAFO_CHL0)
#define REGISTER_DAFO_CHL_CS8                   0x0
#define REGISTER_DAFO_CHL_CS7                   (REGISTER_DAFO_CHL0)
#define REGISTER_DAFO_CHL_CS6                   (REGISTER_DAFO_CHL1)
#define REGISTER_DAFO_CHL_CS5 \
    (REGISTER_DAFO_CHL1 | REGISTER_DAFO_CHL0)

/* RFC. */
#define REGISTER_RFC_OFFSET                     0x28
#define REGISTER_RFC_DPS                        0x40
#define REGISTER_RFC_DXS                        0x20
#define REGISTER_RFC_RFDF                       0x10
#define REGISTER_RFC_RFTH1                      0x8
#define REGISTER_RFC_RFTH0                      0x4
#define REGISTER_RFC_TCDE                       0x1

#define REGISTER_RFC_RFTH_MASK \
    (REGISTER_RFC_RFTH1 | REGISTER_RFC_RFTH0)

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
#define REGISTER_CCR1_ODS                       0x10
#define REGISTER_CCR1_BCR                       0x8
#define REGISTER_CCR1_CM2                       0x4
#define REGISTER_CCR1_CM1                       0x2
#define REGISTER_CCR1_CM0                       0x1

#define REGISTER_CCR1_CM_MASK \
    (REGISTER_CCR1_CM2 | REGISTER_CCR1_CM1 | REGISTER_CCR1_CM0)

/* CCR2. */
#define REGISTER_CCR2_OFFSET                    0x2e
#define REGISTER_CCR2_SOC1                      0x80
#define REGISTER_CCR2_BR9                       0x80
#define REGISTER_CCR2_SOC0                      0x40
#define REGISTER_CCR2_BR8                       0x40
#define REGISTER_CCR2_BDF                       0x20
#define REGISTER_CCR2_XCS0                      0x20
#define REGISTER_CCR2_SSEL                      0x10
#define REGISTER_CCR2_RCS0                      0x10
#define REGISTER_CCR2_TOE                       0x8
#define REGISTER_CCR2_RWX                       0x4
#define REGISTER_CCR2_DIV                       0x1

#define REGISTER_CCR2_BR_MASK \
    (REGISTER_CCR2_BR8 | REGISTER_CCR2_BR9)

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

#define REGISTER_BGR_EN_MASK                    0x3f
#define REGISTER_BGR_EM_MASK                    0xc0

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
#define REGISTER_ISR0_TCD                       0x80
#define REGISTER_ISR0_TIME                      0x40
#define REGISTER_ISR0_PERR                      0x20
#define REGISTER_ISR0_FERR                      0x10
#define REGISTER_ISR0_PLLA                      0x8
#define REGISTER_ISR0_CDSC                      0x4
#define REGISTER_ISR0_RFO                       0x2
#define REGISTER_ISR0_RPF                       0x1

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
#define REGISTER_CCR4_MCK4                      0x80
#define REGISTER_CCR4_EBRG                      0x40
#define REGISTER_CCR4_TST1                      0x20
#define REGISTER_CCR4_ICD                       0x10

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
     * Each channel has dedicated pins for providing receive and transmit clock
     * sources. These dedicated pins are a subset of a larger set of selectable
     * clock sources.
     */
    unsigned int rxclock;
    unsigned int txclock;

    CharBackend chardev;

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

    /*
     * The controller has two pins: XTAL1 and XTAL2. These pins can be used
     * together with a crystal and oscillator to provide a clock source.
     * Alternatively, XTAL1 can provide an externally generated clock source.
     * These configurations are mutually exclusive.
     */
    unsigned int xtal;

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

static void escc2_channel_irq_event(ESCC2ChannelState *channel,
        uint8_t status_register, uint8_t event)
{
    /*
     * Ensure that event does not have more than one bit set when calling this
     * function.
     */
    uint8_t mask, tmp;

    switch (status_register) {
    case REGISTER_ISR0:
        mask = REGISTER_READ(channel, REGISTER_IMR0);
        break;
    case REGISTER_ISR1:
        mask = REGISTER_READ(channel, REGISTER_IMR1);
        break;
    default:
        g_assert_not_reached();
    }

    if ((event & ~(mask))
            || (REGISTER_READ(channel, REGISTER_IPC) & REGISTER_IPC_VIS)) {
        tmp = REGISTER_READ(channel, status_register);
        tmp |= event;
        REGISTER_WRITE(channel, status_register, tmp);
    }

    if (event & ~(mask)) {
        escc2_irq_update(channel->controller);
    }
}

static float escc2_channel_baud_rate_generate(ESCC2ChannelState *channel,
        unsigned int clock)
{
    /*
     * Each channel has an independent baud rate generator. This baud rate
     * generator can act as a clock source for receiving, transmitting, and/or
     * for the DPLL.
     */
    int k, n, m;
    uint8_t ccr2 = REGISTER_READ(channel, REGISTER_CCR2);
    uint8_t bgr = REGISTER_READ(channel, REGISTER_BGR);

    if (REGISTER_READ(channel, REGISTER_CCR2) & REGISTER_CCR2_BDF) {
        /* The baud rate division factor k relies on BGR. */
        if (REGISTER_READ(channel, REGISTER_CCR4) & REGISTER_CCR4_EBRG) {
            /* Enhanced mode. */
            n = bgr & REGISTER_BGR_EN_MASK;
            m = ((ccr2 & REGISTER_CCR2_BR_MASK) >> 6)
                | ((bgr & REGISTER_BGR_EM_MASK) >> 6);
            k = (n + 1) * (2 * m);
        } else {
            /* Standard mode. */
            n = ((ccr2 & REGISTER_CCR2_BR_MASK) << 2) | bgr;
            k = (n + 1) * 2;
        }
    } else {
        k = 1;
    }

    return (float) clock / (16 * k);
}

static void escc2_channel_io_speed(ESCC2ChannelState *channel, float *input,
        float *output)
{
    /*
     * The receive and transmit speed can be configured to leverage dedicated
     * receive and transmit clock source pins, the channel independent baud rate
     * generator, the DPLL for handling clock synchronisation, the onboard
     * oscillator, and a designated master clock. Different combinations of
     * these are selected by specifying the clock mode and submode.
     *
     * Note: The DPLL, to function correctly, requires a clock source with a
     * frequency 16 times the nominal bit rate so that the DPLL can synchronise
     * the clock with the input stream. When the DPLL is used, the frequency
     * must be divided by 16.
     */
    unsigned int mode = REGISTER_READ(channel, REGISTER_CCR1)
        & REGISTER_CCR1_CM_MASK;
    unsigned int submode = REGISTER_READ(channel, REGISTER_CCR2)
        & REGISTER_CCR2_SSEL;

    /* Clock modes are numbered 0 through 7. */
    switch (mode) {
    case 0:
        *input = channel->rxclock;
        if (!submode) {
            /* 0a. */
            *output = channel->txclock;
        } else {
            /* 0b. */
            *output = escc2_channel_baud_rate_generate(channel,
                    channel->controller->xtal);
        }
        break;
    case 1:
        *input = channel->rxclock;
        *output = *input;
        break;
    case 2:
        *input = escc2_channel_baud_rate_generate(channel, channel->rxclock)
            / 16;
        if (!(REGISTER_READ(channel, REGISTER_CCR2)
                    & REGISTER_CCR2_SSEL)) {
            /* 2a. */
            *output = channel->txclock;
        } else {
            /* 2b. */
            *output = *input;
        }
        break;
    case 3:
        *input = escc2_channel_baud_rate_generate(channel, channel->rxclock);
        if (!(REGISTER_READ(channel, REGISTER_CCR2) & REGISTER_CCR2_SSEL)) {
            /* 3a. */
            *input /= 16;
        }
        *output = *input;
        break;
    case 4:
        *input = channel->controller->xtal;
        *output = *input;
    case 5:
        *input = channel->rxclock;
        *output = *input;
    case 6:
        *input = escc2_channel_baud_rate_generate(channel,
                channel->controller->xtal) / 16;
        if (!(REGISTER_READ(channel, REGISTER_CCR2) & REGISTER_CCR2_SSEL)) {
            /* 6a. */
            *output = channel->txclock;
        } else {
            /* 6b. */
            *output = *input;
        }
        break;
    case 7:
        *input = escc2_channel_baud_rate_generate(channel,
                channel->controller->xtal);
        if (!(REGISTER_READ(channel, REGISTER_CCR2) & REGISTER_CCR2_SSEL)) {
            /* 7a. */
            *input /= 16;
        }
        *output = *input;
        break;
    default:
        g_assert_not_reached();
    }
}

static void escc2_channel_parameters_update(ESCC2ChannelState *channel)
{
    uint8_t dafo;
    float ispeed, ospeed;
    QEMUSerialSetParams ssp;

    if (!qemu_chr_fe_backend_connected(&channel->chardev)) {
        return;
    }

    /* Check if parity is enabled. */
    dafo = REGISTER_READ(channel, REGISTER_DAFO);
    if (dafo & REGISTER_DAFO_PARE) {
        /* Determine the parity. */
        switch (dafo & REGISTER_DAFO_PAR_MASK) {
        case REGISTER_DAFO_PAR_SPACE:
        case REGISTER_DAFO_PAR_MARK:
            /*
             * XXX: QEMU doesn't support stick parity yet. Silently fail and
             * fall to the next case.
             */
        case REGISTER_DAFO_PAR_ODD:
            ssp.parity = 'O';
            break;
        case REGISTER_DAFO_PAR_EVEN:
            ssp.parity = 'E';
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        ssp.parity = 'N';
    }

    /* Determine the number of data bits. */
    switch (dafo & REGISTER_DAFO_CHL_MASK) {
    case REGISTER_DAFO_CHL_CS8:
        ssp.data_bits = 8;
        break;
    case REGISTER_DAFO_CHL_CS7:
        ssp.data_bits = 7;
        break;
    case REGISTER_DAFO_CHL_CS6:
        ssp.data_bits = 6;
        break;
    case REGISTER_DAFO_CHL_CS5:
        ssp.data_bits = 5;
        break;
    default:
        g_assert_not_reached();
    }

    /* Determine the number of stop bits. */
    if (dafo & REGISTER_DAFO_STOP) {
        ssp.stop_bits = 2;
    } else {
        ssp.stop_bits = 1;
    }

    /*
     * XXX: QEMU doesn't support configurations with different input/output
     * speeds yet so the input speed is used for both.
     */
    escc2_channel_io_speed(channel, &ispeed, &ospeed);
    ssp.speed = ispeed;

    qemu_chr_fe_ioctl(&channel->chardev, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
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

static void escc2_channel_command(ESCC2ChannelState *channel)
{
    uint8_t tmp, command;

    command = REGISTER_READ(channel, REGISTER_CMDR);
    trace_escc2_channel_command(CHANNEL_CHAR(channel), command);

    if (command & REGISTER_CMDR_RRES) {
        memset(channel->fifo_receive, 0, sizeof(channel->fifo_receive));
        REGISTER_WRITE(channel, REGISTER_RBCL, 0);

        tmp = REGISTER_READ(channel, REGISTER_STAR);
        tmp &= ~(REGISTER_STAR_RFNE);
        REGISTER_WRITE(channel, REGISTER_STAR, tmp);
    }
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
        escc2_channel_command(channel);
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
        escc2_channel_parameters_update(channel);
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
        escc2_channel_parameters_update(channel);
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
        escc2_channel_parameters_update(channel);
        break;
    case REGISTER_TIC_OFFSET:
        REGISTER_WRITE(channel, REGISTER_TIC, value);
        qemu_chr_fe_write_all(&channel->chardev, (uint8_t *)&value, 1);
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
        escc2_channel_parameters_update(channel);
        break;
    default:
        /* Registers do not exhaustively cover the addressable region. */
        break;
    }

    trace_escc2_mem_write(CHANNEL_CHAR(channel), offset, value);
}

static unsigned int escc2_channel_rfifo_threshold(ESCC2ChannelState *channel)
{
    unsigned int threshold;

    switch (REGISTER_READ(channel, REGISTER_RFC) & REGISTER_RFC_RFTH_MASK) {
    case 0:
        threshold = 1;
        break;
    case 1:
        threshold = 4;
        break;
    case 2:
        threshold = 16;
        break;
    case 3:
        threshold = 32;
        break;
    default:
        g_assert_not_reached();
    }

    return threshold;
}

static int escc2_channel_chardev_can_receive(void *opaque)
{
    uint8_t tmp;
    ESCC2ChannelState *channel = opaque;
    unsigned int threshold = escc2_channel_rfifo_threshold(channel);

    tmp = REGISTER_READ(channel, REGISTER_RBCL);
    if (threshold > tmp) {
        return threshold - tmp;
    } else {
        return 0;
    }
}

static void escc2_channel_chardev_receive(void *opaque, const uint8_t *buf,
        int size)
{
    uint8_t tmp, rbcl;
    unsigned int i, nbytes;
    ESCC2ChannelState *channel = opaque;

    /* Determine the number of characters that can be safely consumed. */
    rbcl = REGISTER_READ(channel, REGISTER_RBCL);
    if (rbcl + size > CHANNEL_FIFO_LENGTH) {
        nbytes = CHANNEL_FIFO_LENGTH - rbcl;
    } else {
        nbytes = size;
    }

    /* Consume characters. */
    for (i = 0; i < nbytes; i++) {
        channel->fifo_receive[rbcl + i] = buf[i];
    }
    REGISTER_WRITE(channel, REGISTER_RBCL, rbcl + nbytes);

    tmp = REGISTER_READ(channel, REGISTER_STAR);
    tmp |= REGISTER_STAR_RFNE;
    REGISTER_WRITE(channel, REGISTER_STAR, tmp);

    if (escc2_channel_chardev_can_receive(channel) == 0) {
        escc2_channel_irq_event(channel, REGISTER_ISR0, REGISTER_ISR0_RPF);
    }
}

static void escc2_realize(DeviceState *dev, Error **errp)
{
    unsigned int i;
    ESCC2ChannelState *channel;
    ESCC2State *controller = ESCC2(dev);

    for (i = 0; i < CHANNEL_COUNT; i++) {
        channel = &controller->channel[i];
        channel->controller = controller;

        if (qemu_chr_fe_backend_connected(&channel->chardev)) {
            qemu_chr_fe_set_handlers(&channel->chardev,
                    escc2_channel_chardev_can_receive,
                    escc2_channel_chardev_receive, NULL, NULL, channel, NULL,
                    true);
        }
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
    unsigned int i;
    ESCC2State *controller = ESCC2(dev);

    for (i = 0; i < CHANNEL_COUNT; i++) {
        qemu_chr_fe_deinit(&controller->channel[i].chardev, false);
    }

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
    DEFINE_PROP_CHR("chardevA", ESCC2State, channel[CHANNEL_A].chardev),
    DEFINE_PROP_CHR("chardevB", ESCC2State, channel[CHANNEL_B].chardev),
    DEFINE_PROP_UINT32("xtal", ESCC2State, xtal, 0),
    DEFINE_PROP_UINT32("rxclockA", ESCC2State, channel[CHANNEL_A].rxclock, 0),
    DEFINE_PROP_UINT32("txclockA", ESCC2State, channel[CHANNEL_A].txclock, 0),
    DEFINE_PROP_UINT32("rxclockB", ESCC2State, channel[CHANNEL_B].rxclock, 0),
    DEFINE_PROP_UINT32("txclockB", ESCC2State, channel[CHANNEL_B].txclock, 0),
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
