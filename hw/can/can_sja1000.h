/*
 * CAN device - SJA1000 chip emulation for QEMU
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014 Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef HW_CAN_SJA1000_H
#define HW_CAN_SJA1000_H

#include "can/can_emu.h"

#define CAN_SJA_MEM_SIZE      128

/* The max size for a message buffer, EFF and DLC=8, DS-p39 */
#define SJA_MSG_MAX_LEN       13
/* The receive buffer size. */
#define SJA_RCV_BUF_LEN       64

//#define DEBUG_CAN
#ifdef DEBUG_CAN
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "[cansja]: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do {} while (0)
#endif

typedef void (CanSJAIrqRaiseLower)(void *opaque);

typedef struct CanSJA1000State {
    /* Some registers ... */
    uint8_t         mode;          /* PeliCAN, addr 0, Mode register, DS-p26 */
                                   /* PeliCAN, addr 1, Command register */
    uint8_t         statusP;       /* PeliCAN, addr 2, Status register, p15 */
    uint8_t         interruptP;    /* PeliCAN, addr 3, Interrupt register */
    uint8_t         interrupt_en;  /* PeliCAN, addr 4, Interrupt Enable register */
    uint8_t         rxmsg_cnt;     /* PeliCAN, addr 29, RX message counter. DS-p49 */
    uint8_t         rxbuf_start;   /* PeliCAN, addr 30, RX buffer start address register, DS-p49 */
    uint8_t         clock;         /* PeliCAN, addr 31, Clock Divider register, DS-p55 */

    uint8_t         code_mask[8];  /* PeliCAN, addr 16~23 */
    uint8_t         tx_buff[13];   /* PeliCAN, addr 96~108, transmit buffer */
                                   /* BasicCAN, addr 10~19, transmit buffer */

    uint8_t         rx_buff[SJA_RCV_BUF_LEN];  /* 32~95, 64bytes */
    uint32_t        rx_ptr;        /* Count by bytes. */
    uint32_t        rx_cnt;        /* Count by bytes. */

    uint8_t         control;       /* BasicCAN, addr 0, Control register */
                                   /* BasicCAN, addr 1, Command register */
    uint8_t         statusB;       /* BasicCAN, addr 2, Status register */
    uint8_t         interruptB;    /* BasicCAN, addr 3, Interrupt register */
    uint8_t         code;          /* BasicCAN, addr 4, Acceptance code register */
    uint8_t         mask;          /* BasicCAN, addr 5, Acceptance mask register */

    qemu_can_filter filter[4];

    QemuMutex       rx_lock;
    CanSJAIrqRaiseLower *irq_raise;
    CanSJAIrqRaiseLower *irq_lower;
    void            *irq_opaque;
    CanBusClientState bus_client;
} CanSJA1000State;

/* PeliCAN mode */
enum SJA1000_PeliCAN_regs {
        SJA_MOD      = 0x00,
/* Command register */
        SJA_CMR      = 0x01,
/* Status register */
        SJA_SR       = 0x02,
/* Interrupt register */
        SJA_IR       = 0x03,
/* Interrupt Enable */
        SJA_IER      = 0x04,
/* Bus Timing register 0 */
        SJA_BTR0     = 0x06,
/* Bus Timing register 1 */
        SJA_BTR1     = 0x07,
/* Output Control register */
        SJA_OCR      = 0x08,
/* Arbitration Lost Capture */
        SJA_ALC      = 0x0b,
/* Error Code Capture */
        SJA_ECC      = 0x0c,
/* Error Warning Limit */
        SJA_EWLR     = 0x0d,
/* RX Error Counter */
        SJA_RXERR    = 0x0e,
/* TX Error Counter */
        SJA_TXERR0   = 0x0e,
        SJA_TXERR1   = 0x0f,
/* Rx Message Counter (number of msgs. in RX FIFO */
        SJA_RMC      = 0x1d,
/* Rx Buffer Start Addr. (address of current MSG) */
        SJA_RBSA     = 0x1e,
/* Transmit Buffer (write) Receive Buffer (read) Frame Information */
        SJA_FRM      = 0x10,
/* ID bytes (11 bits in 0 and 1 or 16 bits in 0,1 and 13 bits in 2,3 (extended)) */
        SJA_ID0      = 0x11, SJA_ID1 = 0x12,
/* ID cont. for extended frames */
        SJA_ID2      = 0x13, SJA_ID3 = 0x14,
/* Data start standard frame */
        SJA_DATS     = 0x13,
/* Data start extended frame */
        SJA_DATE     = 0x15,
/* Acceptance Code (4 bytes) in RESET mode */
        SJA_ACR0     = 0x10,
/* Acceptance Mask (4 bytes) in RESET mode */
        SJA_AMR0     = 0x14,
/* 4 bytes */
        SJA_PeliCAN_AC_LEN = 4,
/* Clock Divider */
        SJA_CDR      = 0x1f
};


/* PeliCAN mode */
enum SJA1000_BasicCAN_regs {
        SJA_BCAN_CTR = 0x00,
/* Command register */
        SJA_BCAN_CMR = 0x01,
/* Status register */
        SJA_BCAN_SR  = 0x02,
/* Interrupt register */
        SJA_BCAN_IR  = 0x03
};

void can_sja_hardware_reset(CanSJA1000State *s);
void can_sja_mem_write(CanSJA1000State *s, hwaddr addr, uint64_t val, unsigned size);
uint64_t can_sja_mem_read(CanSJA1000State *s, hwaddr addr, unsigned size);
int can_sja_connect_to_bus(CanSJA1000State *s, CanBusState *bus);
void can_sja_disconnect(CanSJA1000State *s);
int can_sja_init(CanSJA1000State *s, CanSJAIrqRaiseLower *irq_raise,
                 CanSJAIrqRaiseLower *irq_lower, void *irq_opaque);
void can_sja_exit(CanSJA1000State *s);

int can_sja_can_receive(CanBusClientState *client);
ssize_t can_sja_receive(CanBusClientState *client, const qemu_can_frame *frames, size_t frames_cnt);

extern const VMStateDescription vmstate_can_sja;

#endif
