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
#include "qemu/osdep.h"
#include "sysemu/char.h"
#include "hw/hw.h"
#include "can/can_emu.h"

#include "can_sja1000.h"

//#define DEBUG_FILTER

static void can_sja_software_reset(CanSJA1000State *s)
{
    s->mode        &= ~0x31;
    s->mode        |= 0x01;
    s->statusP     &= ~0x37;
    s->statusP     |= 0x34;

    s->rxbuf_start = 0x00;
    s->rxmsg_cnt   = 0x00;
    s->rx_cnt      = 0x00;
}

void can_sja_hardware_reset(CanSJA1000State *s)
{
    /* Reset by hardware, p10 */
    s->mode        = 0x01;
    s->statusP     = 0x3c;
    s->interruptP  = 0x00;
    s->clock       = 0x00;
    s->rxbuf_start = 0x00;
    s->rxmsg_cnt   = 0x00;
    s->rx_cnt      = 0x00;

    s->control     = 0x01;
    s->statusB     = 0x0c;
    s->interruptB  = 0x00;

    s->irq_lower(s->irq_opaque);
}


/* Details in DS-p22, what we need to do here is to test the data. */
static int can_sja_accept_filter(CanSJA1000State *s, const qemu_can_frame *frame)
{
    uint8_t tmp1, tmp2;

    if (s->clock & 0x80) { /* PeliCAN Mode */
        if (s->mode & (1 << 3)) { /* Single mode. */
            if (!(frame->can_id & (1 << 31))) { /* SFF */
                if (frame->can_id & (1 << 30)) { /* RTR */
                    return 1;
                }
                if (frame->can_dlc == 0) {
                    return 1;
                }
                if (frame->can_dlc == 1) {
                    if ((frame->data[0] & ~(s->code_mask[6])) ==
                       (s->code_mask[2] & ~(s->code_mask[6]))) {
                        return 1;
                    }
                }
                if (frame->can_dlc >= 2) {
                    if (((frame->data[0] & ~(s->code_mask[6])) ==
                       (s->code_mask[2] & ~(s->code_mask[6]))) &&
                       ((frame->data[1] & ~(s->code_mask[7])) ==
                       (s->code_mask[3] & ~(s->code_mask[7])))) {
                        return 1;
                    }
                }
                return 0;
            }
        } else { /* Dual mode */
            if (!(frame->can_id & (1 << 31))) { /* SFF */
                if (((s->code_mask[0] & ~s->code_mask[4]) ==
                    (((frame->can_id >> 3) & 0xff) & ~s->code_mask[4])) &&
                    (((s->code_mask[1] & ~s->code_mask[5]) & 0xe0) ==
                    (((frame->can_id << 5) & ~s->code_mask[5]) & 0xe0))) {
                    if (frame->can_dlc == 0) {
                        return 1;
                    } else {
                        tmp1 = ((s->code_mask[1] << 4) & 0xf0) |
                              (s->code_mask[2] & 0x0f);
                        tmp2 = ((s->code_mask[5] << 4) & 0xf0) |
                              (s->code_mask[6] & 0x0f);
                        tmp2 = ~tmp2;
                        if ((tmp1 & tmp2) == (frame->data[0] & tmp2)) {
                            return 1;
                        }
                        return 0;
                    }
                }
            }
        }
    }

    return 1;
}

#ifdef DEBUG_FILTER
static void can_display_msg(const qemu_can_frame *msg)
{
    int i;

    printf("%03X [%01d] -", (msg->can_id & 0x1fffffff), msg->can_dlc);
    if (msg->can_id & (1 << 31)) {
        printf("EFF ");
    } else {
        printf("SFF ");
    }
    if (msg->can_id & (1 << 30)) {
        printf("RTR-");
    } else {
        printf("DAT-");
    }
    for (i = 0; i < msg->can_dlc; i++) {
        printf("  %02X", msg->data[i]);
    }
    for (; i < 8; i++) {
        printf("    ");
    }
    fflush(stdout);
}
#endif
static void buff2frameP(const uint8_t *buff, qemu_can_frame *frame)
{
    uint8_t i;

    frame->can_id = 0;
    if (buff[0] & 0x40) { /* RTR */
        frame->can_id = 0x01 << 30;
    }
    frame->can_dlc = buff[0] & 0x0f;

    if (buff[0] & 0x80) { /* Extended */
        frame->can_id |= 0x01 << 31;
        frame->can_id |= buff[1] << 21; /* ID.28~ID.21 */
        frame->can_id |= buff[2] << 13; /* ID.20~ID.13 */
        frame->can_id |= buff[3] << 05;
        frame->can_id |= buff[4] >> 03;
        for (i = 0; i < frame->can_dlc; i++) {
            frame->data[i] = buff[5+i];
        }
        for (; i < 8; i++) {
            frame->data[i] = 0;
        }
    } else {
        frame->can_id |= buff[1] << 03;
        frame->can_id |= buff[2] >> 05;
        for (i = 0; i < frame->can_dlc; i++) {
            frame->data[i] = buff[3+i];
        }
        for (; i < 8; i++) {
            frame->data[i] = 0;
        }
    }
}


static void buff2frameB(const uint8_t *buff, qemu_can_frame *frame)
{
    uint8_t i;

    frame->can_id = ((buff[0] << 3) & (0xff << 3)) + ((buff[1] >> 5) & 0x07);
    if (buff[1] & 0x10) { /* RTR */
        frame->can_id = 0x01 << 30;
    }
    frame->can_dlc = buff[1] & 0x0f;

    for (i = 0; i < frame->can_dlc; i++) {
        frame->data[i] = buff[2+i];
    }
    for (; i < 8; i++) {
        frame->data[i] = 0;
    }
}


static int frame2buffP(const qemu_can_frame *frame, uint8_t *buff)
{
    int i, count = 0;

    if (frame->can_id & (1 << 29)) { /* error frame, NOT support now. */
        return -1;
    }

    buff[count] = 0x0f & frame->can_dlc; /* DLC */
    if (frame->can_id & (1 << 30)) { /* RTR */
        buff[count] |= (1 << 6);
    }
    if (frame->can_id & (1 << 31)) { /* EFF */
        buff[count] |= (1 << 7);
        buff[++count] = (frame->can_id >> 21) & 0xff; /* ID.28~ID.21 */
        buff[++count] = (frame->can_id >> 13) & 0xff; /* ID.20~ID.13 */
        buff[++count] = (frame->can_id >> 05) & 0xff; /* ID.12~ID.05 */
        buff[++count] = (frame->can_id << 03) & 0xf8; /* ID.04~ID.00,x,x,x */
        for (i = 0; i < frame->can_dlc; i++) {
            buff[++count] = frame->data[i];
        }

        return count + 1;
    } else { /* SFF */
        buff[++count] = (frame->can_id >> 03) & 0xff; /* ID.10~ID.03 */
        buff[++count] = (frame->can_id << 05) & 0xe0; /* ID.02~ID.00,x,x,x,x,x */
        for (i = 0; i < frame->can_dlc; i++) {
            buff[++count] = frame->data[i];
        }

        return count + 1;
    }

    return -1;
}

static int frame2buffB(const qemu_can_frame *frame, uint8_t *buff)
{
    int i, count = 0;

    if ((frame->can_id & (1 << 31)) || /* EFF, not support for BasicMode. */
       (frame->can_id & (1 << 29))) {  /* or Error frame, NOT support now. */
        return -1;
    }


    buff[count++] = 0xff & (frame->can_id >> 3);
    buff[count] = 0xe0 & (frame->can_id << 5);
    if (frame->can_id & (1 << 30)) { /* RTR */
        buff[count] |= (1 << 4);
    }
    buff[count++] |= frame->can_dlc & 0x0f;
    for (i = 0; i < frame->can_dlc; i++) {
        buff[count++] = frame->data[i];
    }

#ifdef DEBUG_FILTER
    printf(" ==2==");
    for (i = 0; i < count; i++) {
        printf(" %02X", buff[i]);
    }
    for (; i < 10; i++) {
        printf("   ");
    }
#endif
    return count;
}

void can_sja_mem_write(CanSJA1000State *s, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_can_frame   frame;
    uint32_t         tmp;
    uint8_t          tmp8, count;


    DPRINTF("write 0x%02llx addr 0x%02x\n", (unsigned long long)val, (unsigned int)addr);

    if (addr > CAN_SJA_MEM_SIZE) {
        return ;
    }

    if (s->clock & 0x80) { /* PeliCAN Mode */
        switch (addr) {
        case SJA_MOD: /* Mode register */
            s->mode = 0x1f & val;
            if ((s->mode & 0x01) && ((val & 0x01) == 0)) {
                /* Go to operation mode from reset mode. */
                if (s->mode & (1 << 3)) { /* Single mode. */
                    /* For EFF */
                    tmp = ((s->code_mask[0] << 21) & (0xff << 21)) |
                          ((s->code_mask[1] << 13) & (0xff << 13)) |
                          ((s->code_mask[2] <<  5) & (0xff <<  5)) |
                          ((s->code_mask[3] >>  3) & 0x1f) |
                          (1 << 31);
                    s->filter[0].can_id = tmp;

                    tmp = ((s->code_mask[4] << 21) & (0xff << 21)) |
                          ((s->code_mask[5] << 13) & (0xff << 13)) |
                          ((s->code_mask[6] <<  5) & (0xff <<  5)) |
                          ((s->code_mask[7] >>  3) & 0x1f) |
                          (7 << 29);
                    s->filter[0].can_mask = ~tmp | (1 << 31);

                    if (s->code_mask[3] & (1 << 2)) { /* RTR */
                        s->filter[0].can_id |= (1 << 30);
                    }
                    if (!(s->code_mask[7] & (1 << 2))) {
                        s->filter[0].can_mask |= (1 << 30);
                    }

                    /* For SFF */
                    tmp = ((s->code_mask[0] <<  3) & (0xff <<  3)) |
                          ((s->code_mask[1] >>  5) & 0x07);
                    s->filter[1].can_id = tmp;

                    tmp = ((s->code_mask[4] <<  3) & (0xff <<  3)) |
                          ((s->code_mask[5] >>  5) & 0x07) |
                          (0xff << 11) | (0xff << 19) | (0x0f << 27);
                    s->filter[1].can_mask = ~tmp | (1 << 31);

                    if (s->code_mask[1] & (1 << 4)) { /* RTR */
                        s->filter[1].can_id |= (1 << 30);
                    }
                    if (!(s->code_mask[5] & (1 << 4))) {
                        s->filter[1].can_mask |= (1 << 30);
                    }

                    can_bus_client_set_filters(&s->bus_client, s->filter, 2);
                } else { /* Dual mode */
                    /* For EFF */
                    tmp = ((s->code_mask[0] << 21) & (0xff << 21)) |
                          ((s->code_mask[1] << 13) & (0xff << 13)) |
                          (1 << 31);
                    s->filter[0].can_id = tmp;

                    tmp = ((s->code_mask[4] << 21) & (0xff << 21)) |
                          ((s->code_mask[5] << 13) & (0xff << 13)) |
                          (0xff << 5) | (0xff >> 3) |
                          (7 << 29);
                    s->filter[0].can_mask = ~tmp | (1 << 31);


                    tmp = ((s->code_mask[2] << 21) & (0xff << 21)) |
                          ((s->code_mask[3] << 13) & (0xff << 13)) |
                          (1 << 31);
                    s->filter[1].can_id = tmp;

                    tmp = ((s->code_mask[6] << 21) & (0xff << 21)) |
                          ((s->code_mask[7] << 13) & (0xff << 13)) |
                          (0xff << 5) | (0xff >> 3) |
                          (7 << 29);
                    s->filter[1].can_mask = ~tmp | (1 << 31);

                    /* For SFF */
                    tmp = ((s->code_mask[0] <<  3) & (0xff <<  3)) |
                          ((s->code_mask[1] >>  5) & 0x07);
                    s->filter[2].can_id = tmp;

                    tmp = ((s->code_mask[4] <<  3) & (0xff <<  3)) |
                          ((s->code_mask[5] >>  5) & 0x07) |
                          (0xff << 11) | (0xff << 19) | (0x0f << 27);
                    s->filter[2].can_mask = ~tmp | (1 << 31);

                    if (s->code_mask[1] & (1 << 4)) { /* RTR */
                        s->filter[2].can_id |= (1 << 30);
                    }
                    if (!(s->code_mask[5] & (1 << 4))) {
                        s->filter[2].can_mask |= (1 << 30);
                    }

                    tmp = ((s->code_mask[2] <<  3) & (0xff <<  3)) |
                          ((s->code_mask[3] >>  5) & 0x07);
                    s->filter[3].can_id = tmp;

                    tmp = ((s->code_mask[6] <<  3) & (0xff <<  3)) |
                          ((s->code_mask[7] >>  5) & 0x07) |
                          (0xff << 11) | (0xff << 19) | (0x0f << 27);
                    s->filter[3].can_mask = ~tmp | (1 << 31);

                    if (s->code_mask[3] & (1 << 4)) { /* RTR */
                        s->filter[3].can_id |= (1 << 30);
                    }
                    if (!(s->code_mask[7] & (1 << 4))) {
                        s->filter[3].can_mask |= (1 << 30);
                    }

                    can_bus_client_set_filters(&s->bus_client, s->filter, 4);
                }

                s->rxmsg_cnt = 0;
                s->rx_cnt = 0;
            }
            break;

        case SJA_CMR: /* Command register. */
            if (0x01 & val) { /* Send transmission request. */
                buff2frameP(s->tx_buff, &frame);
#ifdef DEBUG_FILTER
                can_display_msg(&frame); printf("\n");
#endif
                s->statusP &= ~(3 << 2); /* Clear transmission complete status, */
                                        /* and Transmit Buffer Status. */
                /* write to the backends. */
                can_bus_client_send(&s->bus_client, &frame, 1);
                s->statusP |= (3 << 2); /* Set transmission complete status, */
                                       /* and Transmit Buffer Status. */
                s->statusP &= ~(1 << 5); /* Clear transmit status. */
                s->interruptP |= 0x02;
                if (s->interrupt_en & 0x02) {
                    s->irq_raise(s->irq_opaque);
                }
            } else if (0x04 & val) { /* Release Receive Buffer */
                if (s->rxmsg_cnt <= 0) {
                    break;
                }

                tmp8 = s->rx_buff[s->rxbuf_start]; count = 0;
                if (tmp8 & (1 << 7)) { /* EFF */
                    count += 2;
                }
                count += 3;
                if (!(tmp8 & (1 << 6))) { /* DATA */
                    count += (tmp8 & 0x0f);
                }
                s->rxbuf_start += count;
                s->rxbuf_start %= SJA_RCV_BUF_LEN;

                s->rx_cnt -= count;
                s->rxmsg_cnt--;
                if (s->rxmsg_cnt == 0) {
                    s->statusP &= ~(1 << 0);
                    s->interruptP &= ~(1 << 0);
                }
                if ((s->interrupt_en & 0x01) && (s->interruptP == 0)) {
                    /* no other interrupts. */
                    s->irq_lower(s->irq_opaque);
                }
            } else if (0x08 & val) { /* Clear data overrun */
                s->statusP &= ~(1 << 1);
                s->interruptP &= ~(1 << 3);
                if ((s->interrupt_en & 0x80) && (s->interruptP == 0)) {
                    /* no other interrupts. */
                    s->irq_lower(s->irq_opaque);
                }
            }
            break;
        case SJA_SR: /* Status register */
        case SJA_IR: /* Interrupt register */
            break; /* Do nothing */
        case SJA_IER: /* Interrupt enable register */
            s->interrupt_en = val;
            break;
        case 16: /* RX frame information addr16-28. */
            s->statusP |= (1 << 5); /* Set transmit status. */
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
            if (s->mode & 0x01) { /* Reset mode */
                if (addr < 24) {
                    s->code_mask[addr - 16] = val;
                }
            } else { /* Operation mode */
                s->tx_buff[addr - 16] = val; /* Store to TX buffer directly. */
            }
            break;
        case SJA_CDR:
            s->clock = val;
            break;
        }
    } else { /* Basic Mode */
        switch (addr) {
        case SJA_BCAN_CTR: /* Control register, addr 0 */
            if ((s->control & 0x01) && ((val & 0x01) == 0)) {
                /* Go to operation mode from reset mode. */
                s->filter[0].can_id = (s->code << 3) & (0xff << 3);
                tmp = (~(s->mask << 3)) & (0xff << 3);
                tmp |= (1 << 31);/* Only Basic CAN Frame. */
                s->filter[0].can_mask = tmp;
                can_bus_client_set_filters(&s->bus_client, s->filter, 1);

                s->rxmsg_cnt = 0;
                s->rx_cnt = 0;
            } else if (!(s->control & 0x01) && !(val & 0x01)) {
                can_sja_software_reset(s);
            }

            s->control = 0x1f & val;
            break;
        case SJA_BCAN_CMR: /* Command register, addr 1 */
            if (0x01 & val) { /* Send transmission request. */
                buff2frameB(s->tx_buff, &frame);
#ifdef DEBUG_FILTER
                can_display_msg(&frame); printf("\n");
#endif
                s->statusB &= ~(3 << 2); /* Clear transmission complete status, */
                                        /* and Transmit Buffer Status. */
                /* write to the backends. */
                can_bus_client_send(&s->bus_client, &frame, 1);
                s->statusB |= (3 << 2); /* Set transmission complete status, */
                                       /* and Transmit Buffer Status. */
                s->statusB &= ~(1 << 5); /* Clear transmit status. */
                s->interruptB |= 0x02;
                if (s->control & 0x04) {
                    s->irq_raise(s->irq_opaque);
                }
            } else if (0x04 & val) { /* Release Receive Buffer */
                if (s->rxmsg_cnt <= 0) {
                    break;
                }

                qemu_mutex_lock(&s->rx_lock);
                tmp8 = s->rx_buff[(s->rxbuf_start + 1) % SJA_RCV_BUF_LEN];
                count = 2 + (tmp8 & 0x0f);
#ifdef DEBUG_FILTER
                printf("\nRelease");
                for (i = 0; i < count; i++) {
                    printf(" %02X", s->rx_buff[(s->rxbuf_start + i) % SJA_RCV_BUF_LEN]);
                }
                for (; i < 11; i++) {
                    printf("   ");
                }
                printf("==== cnt=%d, count=%d\n", s->rx_cnt, count);
#endif
                s->rxbuf_start += count;
                s->rxbuf_start %= SJA_RCV_BUF_LEN;
                s->rx_cnt -= count;
                s->rxmsg_cnt--;
                qemu_mutex_unlock(&s->rx_lock);

                if (s->rxmsg_cnt == 0) {
                    s->statusB &= ~(1 << 0);
                    s->interruptB &= ~(1 << 0);
                }
                if ((s->control & 0x02) && (s->interruptB == 0)) {
                    /* no other interrupts. */
                    s->irq_lower(s->irq_opaque);
                }
            } else if (0x08 & val) { /* Clear data overrun */
                s->statusB &= ~(1 << 1);
                s->interruptB &= ~(1 << 3);
                if ((s->control & 0x10) && (s->interruptB == 0)) {
                    /* no other interrupts. */
                    s->irq_lower(s->irq_opaque);
                }
            }
            break;
        case 4:
            s->code = val;
            break;
        case 5:
            s->mask = val;
            break;
        case 10:
            s->statusB |= (1 << 5); /* Set transmit status. */
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
            if ((s->control & 0x01) == 0) { /* Operation mode */
                s->tx_buff[addr - 10] = val; /* Store to TX buffer directly. */
            }
            break;
        case SJA_CDR:
            s->clock = val;
            break;
        }
    }
}

uint64_t can_sja_mem_read(CanSJA1000State *s, hwaddr addr, unsigned size)
{
    uint64_t temp = 0;

    DPRINTF("read addr 0x%x", (unsigned int)addr);

    if (addr > CAN_SJA_MEM_SIZE) {
        return 0;
    }

    if (s->clock & 0x80) { /* PeliCAN Mode */
        switch (addr) {
        case SJA_MOD: /* Mode register, addr 0 */
            temp = s->mode;
            break;
        case SJA_CMR: /* Command register, addr 1 */
            temp = 0x00; /* Command register, cannot be read. */
            break;
        case SJA_SR: /* Status register, addr 2 */
            temp = s->statusP;
            break;
        case SJA_IR: /* Interrupt register, addr 3 */
            temp = s->interruptP;
            s->interruptP = 0;
            if (s->rxmsg_cnt) {
                s->interruptP |= (1 << 0); /* Receive interrupt. */
                break;
            }
            s->irq_lower(s->irq_opaque);
            break;
        case SJA_IER: /* Interrupt enable register, addr 4 */
            temp = s->interrupt_en;
            break;
        case 5: /* Reserved */
        case 6: /* Bus timing 0, hardware related, not support now. */
        case 7: /* Bus timing 1, hardware related, not support now. */
        case 8: /* Output control register, hardware related, not support now. */
        case 9: /* Test. */
        case 10: /* Reserved */
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
            temp = 0x00;
            break;

        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
            if (s->mode & 0x01) { /* Reset mode */
                if (addr < 24) {
                    temp = s->code_mask[addr - 16];
                } else {
                    temp = 0x00;
                }
            } else { /* Operation mode */
                temp = s->rx_buff[(s->rxbuf_start + addr - 16) % SJA_RCV_BUF_LEN];
            }
            break;
        case SJA_CDR:
            temp = s->clock;
            break;
        default:
            temp = 0xff;
        }
    } else { /* Basic Mode */
        switch (addr) {
        case SJA_BCAN_CTR: /* Control register, addr 0 */
            temp = s->control;
            break;
        case SJA_BCAN_SR: /* Status register, addr 2 */
            temp = s->statusB;
            break;
        case SJA_BCAN_IR: /* Interrupt register, addr 3 */
            temp = s->interruptB;
            s->interruptB = 0;
            if (s->rxmsg_cnt) {
                s->interruptB |= (1 << 0); /* Receive interrupt. */
                break;
            }
            s->irq_lower(s->irq_opaque);
            break;
        case 4:
            temp = s->code;
            break;
        case 5:
            temp = s->mask;
            break;
        case 20:
#ifdef DEBUG_FILTER
            printf("Read   ");
#endif
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
            temp = s->rx_buff[(s->rxbuf_start + addr - 20) % SJA_RCV_BUF_LEN];
#ifdef DEBUG_FILTER
            printf(" %02X", (unsigned int)(temp & 0xff));
#endif
            break;
        case 31:
            temp = s->clock;
            break;
        default:
            temp = 0xff;
            break;
        }
    }
    DPRINTF("     %d bytes of 0x%lx from addr %d\n", size, (long unsigned int)temp, (int)addr);

    return temp;
}

int can_sja_can_receive(CanBusClientState *client)
{
    CanSJA1000State *s = container_of(client, CanSJA1000State, bus_client);

    if (s->clock & 0x80) { /* PeliCAN Mode */
        if (s->mode & 0x01) { /* reset mode. */
            return 0;
        }
    } else { /* BasicCAN mode */
        if (s->control & 0x01) {
            return 0;
        }
    }

    return 1; /* always return 1, when operation mode */
}

ssize_t can_sja_receive(CanBusClientState *client, const qemu_can_frame *frames, size_t frames_cnt)
{
    CanSJA1000State *s = container_of(client, CanSJA1000State, bus_client);
    static uint8_t rcv[SJA_MSG_MAX_LEN];
    int i;
    int ret = -1;
    const qemu_can_frame *frame = frames;

    if (frames_cnt <= 0) {
        return 0;
    }
#ifdef DEBUG_FILTER
    printf("#################################################\n");
    can_display_msg(frame);
#endif

    qemu_mutex_lock(&s->rx_lock); /* Just do it quickly :) */
    if (s->clock & 0x80) { /* PeliCAN Mode */
        s->statusP |= (1 << 4); /* the CAN controller is receiving a message */

        if (can_sja_accept_filter(s, frame) == 0) {
            s->statusP &= ~(1 << 4);
#ifdef DEBUG_FILTER
            printf("     NOT\n");
#endif
            goto fail;
        }

        ret = frame2buffP(frame, rcv);
        if (ret < 0) {
            s->statusP &= ~(1 << 4);
#ifdef DEBUG_FILTER
            printf("     ERR\n");
#endif
            goto fail; /* maybe not support now. */
        }

        if (s->rx_cnt + ret > SJA_RCV_BUF_LEN) { /* Data overrun. */
            s->statusP |= (1 << 1); /* Overrun status */
            s->interruptP |= (1 << 3);
            if (s->interrupt_en & (1 << 3)) { /* Overrun interrupt enable */
                s->irq_raise(s->irq_opaque);
            }
            s->statusP &= ~(1 << 4);
#ifdef DEBUG_FILTER
            printf("     OVER\n");
#endif
            goto fail;
        }
        s->rx_cnt += ret;
        s->rxmsg_cnt++;
#ifdef DEBUG_FILTER
        printf("     OK\n");
#endif

        for (i = 0; i < ret; i++) {
            s->rx_buff[(s->rx_ptr++) % SJA_RCV_BUF_LEN] = rcv[i];
        }
        s->rx_ptr %= SJA_RCV_BUF_LEN; /* update the pointer. */

        s->statusP |= 0x01; /* Set the Receive Buffer Status. DS-p23 */
        s->interruptP |= 0x01;
        s->statusP &= ~(1 << 4);
        s->statusP |= (1 << 0);
        if (s->interrupt_en & 0x01) { /* Receive Interrupt enable. */
            s->irq_raise(s->irq_opaque);
        }
    } else { /* BasicCAN mode */
        s->statusB |= (1 << 4); /* the CAN controller is receiving a message */

        ret = frame2buffB(frame, rcv);
        if (ret < 0) {
            s->statusB &= ~(1 << 4);
#ifdef DEBUG_FILTER
            printf("     NOT\n");
#endif
            goto fail; /* maybe not support now. */
        }

        if (s->rx_cnt + ret > SJA_RCV_BUF_LEN) { /* Data overrun. */
            s->statusB |= (1 << 1); /* Overrun status */
            s->statusB &= ~(1 << 4);
            s->interruptB |= (1 << 3);
            if (s->control & (1 << 4)) { /* Overrun interrupt enable */
                s->irq_raise(s->irq_opaque);
            }
#ifdef DEBUG_FILTER
            printf("     OVER\n");
#endif
            goto fail;
        }
        s->rx_cnt += ret;
        s->rxmsg_cnt++;
#ifdef DEBUG_FILTER
        printf("     OK\n");
        printf("RCV B ret=%2d, ptr=%2d cnt=%2d msg=%2d\n", ret, s->rx_ptr, s->rx_cnt, s->rxmsg_cnt);
#endif
        for (i = 0; i < ret; i++) {
            s->rx_buff[(s->rx_ptr++) % SJA_RCV_BUF_LEN] = rcv[i];
        }
        s->rx_ptr %= SJA_RCV_BUF_LEN; /* update the pointer. */

        s->statusB |= 0x01; /* Set the Receive Buffer Status. DS-p15 */
        s->statusB &= ~(1 << 4);
        s->interruptB |= 0x01;
        if (s->control & 0x02) { /* Receive Interrupt enable. */
            s->irq_raise(s->irq_opaque);
        }
    }
    ret = 1;
fail:
    qemu_mutex_unlock(&s->rx_lock);

    return ret;
}

static CanBusClientInfo can_sja_bus_client_info = {
    .can_receive = can_sja_can_receive,
    .receive = can_sja_receive,
    .cleanup = NULL,
    .poll = NULL
};


int can_sja_connect_to_bus(CanSJA1000State *s, CanBusState *bus)
{
    s->bus_client.info = &can_sja_bus_client_info;

    if (can_bus_insert_client(bus, &s->bus_client) < 0) {
        return -1;
    }

    return 0;
}

void can_sja_disconnect(CanSJA1000State *s)
{
    can_bus_remove_client(&s->bus_client);
}

int can_sja_init(CanSJA1000State *s, CanSJAIrqRaiseLower *irq_raise,
                 CanSJAIrqRaiseLower *irq_lower, void *irq_opaque)
{
    qemu_mutex_init(&s->rx_lock);

    s->irq_raise = irq_raise;
    s->irq_lower = irq_lower;
    s->irq_opaque = irq_opaque;

    s->irq_lower(s->irq_opaque);

    can_sja_hardware_reset(s);

    return 0;
}

void can_sja_exit(CanSJA1000State *s)
{
    qemu_mutex_destroy(&s->rx_lock);
}

const VMStateDescription vmstate_qemu_can_filter = {
    .name = "qemu_can_filter",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(can_id, qemu_can_filter),
        VMSTATE_UINT32(can_mask, qemu_can_filter),
        VMSTATE_END_OF_LIST()
    }
};

/* VMState is needed for live migration of QEMU images */
const VMStateDescription vmstate_can_sja = {
    .name = "can_sja",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(mode, CanSJA1000State),

        VMSTATE_UINT8(statusP, CanSJA1000State),
        VMSTATE_UINT8(interruptP, CanSJA1000State),
        VMSTATE_UINT8(interrupt_en, CanSJA1000State),
        VMSTATE_UINT8(rxmsg_cnt, CanSJA1000State),
        VMSTATE_UINT8(rxbuf_start, CanSJA1000State),
        VMSTATE_UINT8(clock, CanSJA1000State),

        VMSTATE_BUFFER(code_mask, CanSJA1000State),
        VMSTATE_BUFFER(tx_buff, CanSJA1000State),

        VMSTATE_BUFFER(rx_buff, CanSJA1000State),

        VMSTATE_UINT32(rx_ptr, CanSJA1000State),
        VMSTATE_UINT32(rx_cnt, CanSJA1000State),

        VMSTATE_UINT8(control, CanSJA1000State),

        VMSTATE_UINT8(statusB, CanSJA1000State),
        VMSTATE_UINT8(interruptB, CanSJA1000State),
        VMSTATE_UINT8(code, CanSJA1000State),
        VMSTATE_UINT8(mask, CanSJA1000State),

        VMSTATE_STRUCT_ARRAY(filter, CanSJA1000State, 4, 0,
                             vmstate_qemu_can_filter, qemu_can_filter),


        VMSTATE_END_OF_LIST()
    }
};
