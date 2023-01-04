/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CAN device - MCP25625 chip model
 *
 * Copyright (c) 2022 SiFive, Inc.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"
#include "hw/ssi/ssi.h"
#include "hw/qdev-properties.h"
#include "hw/net/can_mcp25625.h"

#include "trace.h"

/*
 * note: filter registers read back 0 unless in config mode
 */
#define OFF_RXFSIDH             (0x0)
#define OFF_RXFSIDL             (0x1)
#define OFF_RXFEID8             (0x2)
#define OFF_RXFEID0             (0x3)

#define RXFSIDL_EXIDE           (1 << 3)
#define RXFSIDL_WRITEMASK       (0xE0 | RXFSIDL_EXIDE | 0x3)

struct rxfilter {
    uint8_t data[4];
};

#define BFPCTRL_WRITEMASK       (0x3F)
#define BFPCTRL_B0BFM           (1 << 0)
#define BFPCTRL_B0BFE           (1 << 2)
#define BFPCTRL_B0BFS           (1 << 4)

#define TXRTSCTRL_WRITEMASK     (0x7)

/* rx mask register offsets (read back 0 unless in config mode) */
#define OFF_RXMSIDH             (0x0)
#define OFF_RXMSIDL             (0x1)
#define OFF_RXMEID8             (0x2)
#define OFF_RXMEID0             (0x3)

#define RXMSIDL_WRITEMASK       (0xE3)

struct rxmask {
    uint8_t data[4];
};

#define CNF3_WRITEMASK          (0xC7)

#define OFF_TXBCTRL             (0x0)
#define TXBCTRL_ABTF            (1 << 6)
#define TXBCTRL_MLOA            (1 << 5)
#define TXBCTRL_TXREQ           (1 << 3)
#define TXBCTRL_TXP1            (1 << 1)
#define TXBCTRL_TXP0            (1 << 0)
#define TXBCTRL_TXP             (TXBCTRL_TXP1 | TXBCTRL_TXP0)
#define TXBCTRL_WRITEMASK       (TXBCTRL_TXREQ | TXBCTRL_TXP)

#define OFF_TXBSIDH             (0x1)

#define OFF_TXBSIDL             (0x2)
#define TXBSIDL_EXIDE           (1 << 3)
/* bits 7..5 are SID[2:0], bits 1..0 are EID[17:16] */
#define TXBSIDL_WRITEMASK       (0xE0 | TXBSIDL_EXIDE | 0x3)

#define OFF_TXBEID8             (0x3)   /* this is EID[15:8] */
#define OFF_TXBEID0             (0x4)   /* this is EID[7:0] */

#define OFF_TXBDLC              (0x5)   /* bits 3..0 are DLC */
#define TXBDLC_RTR              (1 << 6)
#define TXBDLC_WRITEMASK        (TXBDLC_RTR | 0xF)

#define OFF_TXBDATA             (0x6)

struct txbuff {
    uint8_t data[14];
};

#define OFF_RXBCTRL             (0x0)
#define RXBCTRL_RXM_ANY         (3 << 5)
#define RXBCTRL_RXM_VALID       (0 << 5)
#define RXBCTRL_RXM_MASK        (3 << 5)

#define RXBCTRL_RXRTR           (1 << 3)
#define RXBCTRL_BUKT            (1 << 2)
#define RXBCTRL_BUKT1           (1 << 1)
/* RXBCTRL0, bit0 = filter hit for message */
/* RXBCTRL1, bits 2..0 show filter hit */
#define RXBCTRL0_WRITEMASK      (RXBCTRL_RXM_MASK | RXBCTRL_BUKT)
#define RXBCTRL1_WRITEMASK      (RXBCTRL_RXM_MASK)

#define OFF_RXBSIDH             (0x1)
/* bits 7..0 = SID[10:3] */

#define OFF_RXBSIDL             (0x2)
#define RXBSIDL_SRR             (1 << 4)
#define RXBSIDL_IDE             (1 << 3)
/* bits 7..5 = SID[2:0], bits 1..0 = EID[17:16] */

#define OFF_RXBEID8             (0x3)   /* bits 7..0 = EID[15:8] */
#define OFF_RXBEID0             (0x4)   /* bits 7..0 = EID[7:0] */

#define OFF_RXBDLC              (0x5)
#define RXBDLC_RTR              (1 << 6)  /* bits 3..0 = number of bytes */

struct rxbuff {
    uint8_t data[14];
};

/*
 * The state of the current spi access, from SSI_INSTRUCTION where the
 * system is waiting for the instruction byte to then what steps are
 * taken given the instruction received.
 */
enum mcp_ssi_state {
    SSI_INSTRUCTION,
    SSI_ADDRESS,
    SSI_WR_DATA,
    SSI_RD_DATA,
    SSI_WAIT,
    SSI_RD_STATUS,
    SSI_RD_RXSTATUS,
    SSI_MODIFY_ADDR,
    SSI_MODIFY_MASK,
    SSI_MODIFY_DATA,
};

#define EFLG_RX1OVR             (1 << 7)
#define EFLG_RX0OVR             (1 << 6)
/* don't think we need any other errors */
#define EFLG_WRITEMASK          (EFLG_RX1OVR | EFLG_RX0OVR)

#define IRQ_MERR        (1 << 7)
#define IRQ_WAKE        (1 << 6)
#define IRQ_ERR         (1 << 5)
#define IRQ_TX2         (1 << 4)
#define IRQ_TX1         (1 << 3)
#define IRQ_TX0         (1 << 2)
#define IRQ_TX(__n)     (1 << ((__n) + 2))
#define IRQ_RX1         (1 << 1)
#define IRQ_RX0         (1 << 0)
#define IRQ_RX(__n)     (1 << (__n))

#define CANSTAT_ICOD_MASK (0x7 << 1)

#define CTRL_REQ_NORMAL (0 << 5)
#define CTRL_REQ_SLEEP  (1 << 5)
#define CTRL_REQ_LOOP   (2 << 5)
#define CTRL_REQ_LISTEN (3 << 5)
#define CTRL_REQ_CFG    (4 << 5)
#define CTRL_REQ_MASK   (7 << 5)

#define CTRL_CLK_EN     (1 << 2)
#define CTRL_DEF_CLK    (CTRL_CLK_EN | 0x3)
#define CTRL_ABAT       (1 << 4)

struct MCP25625State;

typedef struct MCP25625State {
    SSIPeripheralClass parent_class;

    qemu_irq            irq;
    qemu_irq            rxb_irq[2];

    CanBusClientState   bus_client;
    CanBusState         *canbus;
    const char          *trace_name;

    /* spi bus state */

    enum mcp_ssi_state ssi_state;
    bool ssi_write;
    bool ssi_only_cfg_rd;
    bool ssi_can_bitmodify;
    uint8_t ssi_addr;
    uint8_t ssi_modify_mask;
    uint8_t *ssi_ptr;
    uint8_t ssi_writemask;
    uint8_t ssi_rxbuff;

    /* internal state */

    uint32_t lastirq;

    /* registers */

    uint8_t canstat;
    uint8_t canctrl;
    uint8_t bfpctrl;
    uint8_t txrtsctrl;
    uint8_t tec;
    uint8_t rec;
    uint8_t cnfs[4];    /* note, cnfs[0] is not used */
    uint8_t caninte;
    uint8_t canintf;
    uint8_t eflg;

    struct txbuff txbuffs[3];
    struct rxbuff rxbuffs[2];
    struct rxfilter rxfilters[6];
    struct rxmask rxmasks[2];
} MCP25625State;

#define trace_name(__s) ((__s)->trace_name)

static ssize_t mcp25625_can_receive(CanBusClientState *client,
                                    const qemu_can_frame *buf, size_t frames_cnt);
static void mcp25625_do_rts(MCP25625State *s, uint32_t tx);
static void mcp25625_update_canctrl(MCP25625State *s, bool wakeup_happened);
static void mcp25625_update_irqs(MCP25625State *s, unsigned flags);

static bool mcp25625_is_in_cfg(MCP25625State *s)
{
    return (s->canstat & CTRL_REQ_MASK) == CTRL_REQ_CFG;
}

static bool mcp25625_is_in_sleep(MCP25625State *s)
{
    return (s->canstat & CTRL_REQ_MASK) == CTRL_REQ_SLEEP;
}

static bool mcp25625_is_in_normal(MCP25625State *s)
{
    return (s->canstat & CTRL_REQ_MASK) == CTRL_REQ_NORMAL;
}

static bool mcp25625_is_in_loopback(MCP25625State *s)
{
    return (s->canstat & CTRL_REQ_MASK) == CTRL_REQ_LOOP;
}

static bool mcp25625_mode_exists(uint8_t mode)
{
    return (mode == CTRL_REQ_NORMAL) ||
           (mode == CTRL_REQ_SLEEP) ||
           (mode == CTRL_REQ_LOOP) ||
           (mode == CTRL_REQ_LISTEN) ||
           (mode == CTRL_REQ_CFG);
}

/*
 * Registers that can be modified with the BIT-MODIFY command:
 * TXBxCTRL, RXBxCTRL, CNF[1-3], CANINTE, CANINTF, EFLG, CANCTRL,
 * BFPCTRL and TXRTSCTRL
 *
 * We currently assume that the A7 bit is jut not used even through it
 * is shown on the SPI diagrams. The last register is 0x7f so we wrap
 * around at that point back to 0x0.
 */
static uint8_t *addr_to_reg(MCP25625State *s, unsigned reg)
{
    unsigned low = reg & 0xf;
    unsigned high = (reg >> 4) & 0x7;

    s->ssi_only_cfg_rd = false;
    s->ssi_writemask = 0xff;
    s->ssi_can_bitmodify = false;

    if (low == 0x0e) {
        s->ssi_writemask = 0x0;
        return &s->canstat;
    }

    if (low == 0x0f) {
        s->ssi_can_bitmodify = true;
        return &s->canctrl;
    }

    switch (high) {
    case 0x0:
        if (low < 12) {
            if (low % 4 == 1) {
                s->ssi_writemask = RXFSIDL_WRITEMASK;
            }
            s->ssi_only_cfg_rd = true;
            return &s->rxfilters[low / 4].data[low % 4];
        }

        s->ssi_can_bitmodify = true;
        if (low == 12) {
            s->ssi_writemask = BFPCTRL_WRITEMASK;
            return &s->bfpctrl;
        }
        if (low == 13) {
            s->ssi_writemask = TXRTSCTRL_WRITEMASK;
            s->ssi_only_cfg_rd = true;
            return &s->txrtsctrl;
        }
        break;

    case 0x01:
        if (low < 12) {
            if (low % 4 == 1) {
                s->ssi_writemask = RXFSIDL_WRITEMASK;
            }
            s->ssi_only_cfg_rd = true;
            return &s->rxfilters[(low / 4) + 3].data[low % 4];
        }
        if (low == 12) {
            s->ssi_writemask = 0x0;
            return &s->tec;
        }
        if (low == 13) {
            s->ssi_writemask = 0x0;
            return &s->rec;
        }
        break;

    case 0x02:
        if (low < 8) {
            if (low % 4 == 1) {
                s->ssi_writemask = RXMSIDL_WRITEMASK;
            }
            s->ssi_only_cfg_rd = true;
            return &s->rxmasks[low / 4].data[low % 4];
        }

        s->ssi_can_bitmodify = true;
        switch (low) {
        case 8:
            s->ssi_writemask = CNF3_WRITEMASK;
            s->ssi_only_cfg_rd = true;
            return &s->cnfs[3];
        case 9:
            s->ssi_only_cfg_rd = true;
            return &s->cnfs[2];
        case 10:
            s->ssi_only_cfg_rd = true;
            return &s->cnfs[1];
        case 11:
            return &s->caninte;
        case 12:
            return &s->canintf;
        case 13:
            s->ssi_writemask = EFLG_WRITEMASK;
            return &s->eflg;
        }
        break;

    case 0x03 ... 0x05:
        if (low == 0) {
            s->ssi_writemask = TXBCTRL_WRITEMASK;
            s->ssi_can_bitmodify = true;
        }
        if (low == 2) {
            s->ssi_writemask = TXBSIDL_WRITEMASK;
        }
        if (low == 5) {
            s->ssi_writemask = TXBDLC_WRITEMASK;
        }
        return &s->txbuffs[high - 3].data[low];

    case 0x06 ... 0x07:
        if (low == 0) {
            s->ssi_writemask =
                    (high == 0x06 ? RXBCTRL0_WRITEMASK : RXBCTRL1_WRITEMASK);
            s->ssi_can_bitmodify = true;
        } else {
            s->ssi_writemask = 0x0;
        }
        return &s->rxbuffs[high - 6].data[low];
    }

    /* we shouldn't really get here */
    return NULL;
}

static void mcp25625_clear_rx(MCP25625State *s)
{
    trace_mcp25625_rx_clear(trace_name(s), s->ssi_rxbuff);
    s->canintf &= ~IRQ_RX(s->ssi_rxbuff);

    mcp25625_update_irqs(s, 0x0);
}

static int mcp25625_cs(SSIPeripheral *ss, bool select)
{
    MCP25625State *s = MCP25625(ss);

    if (select) {
        /* chip-select has gone inactive */
        if (s->ssi_rxbuff != 0xff) {
            mcp25625_clear_rx(s);
        }
    } else {
        /* chip-select is going active */
        s->ssi_rxbuff = 0xff;
        s->ssi_state = SSI_INSTRUCTION;
    }

    return 0;
}

static unsigned irq_flags_to_icod(uint32_t val)
{
    if (val & IRQ_ERR) {
        return 1 << 1;
    } else if (val & IRQ_WAKE) {
        return 2 << 1;
    } else if (val & IRQ_TX(0)) {
        return 3 << 1;
    } else if (val & IRQ_TX(1)) {
        return 4 << 1;
    } else if (val & IRQ_TX(2)) {
        return 5 << 1;
    } else if (val & IRQ_RX(0)) {
        return 6 << 1;
    } else if (val & IRQ_RX(1)) {
        return 7 << 1;
    }

    return 0;
}

/*
 * RXBFx pins can be mapped to respective buffer full status of the
 * CANINTF bits (3.7.3 in datasheet). The pin can either be disabled
 * an output or an indicator if the relevant buffer is full.
 */
static void mcp25625_update_rxbf(MCP25625State *s, unsigned buff)
{
    uint8_t bfpctrl = s->bfpctrl;
    unsigned val = 1;

    /* buffer 1 is just buffer 0 controls shifted down by 1 */
    if (buff != 0) {
        bfpctrl >>= 1;
    }

    if (bfpctrl & BFPCTRL_B0BFE) {
        if (bfpctrl & BFPCTRL_B0BFM) {
            val = (s->canintf & IRQ_RX(buff)) ? 0 : 1;
        } else {
            val = (bfpctrl & BFPCTRL_B0BFS) ? 1 : 0;
        }
    } else {
        val = 1;
    }

    qemu_set_irq(s->rxb_irq[buff], val);
}

static void mcp25625_update_irqs(MCP25625State *s, unsigned flags)
{
    uint8_t newirq;

    trace_mcp25625_irq_update(trace_name(s), flags);
    s->canintf |= flags;
    newirq = s->canintf & s->caninte;

    if (s->lastirq != newirq) {
        trace_mcp25625_irq_change(trace_name(s), s->lastirq, newirq);

        s->lastirq = newirq;
        s->canstat &= ~CANSTAT_ICOD_MASK;
        if (newirq != 0x0) {
            s->canstat |= irq_flags_to_icod(newirq);
        }

        if ((newirq & IRQ_WAKE) && mcp25625_is_in_sleep(s)) {
            mcp25625_update_canctrl(s, true);
        }
    }

    mcp25625_update_rxbf(s, 0);
    mcp25625_update_rxbf(s, 1);
    qemu_set_irq(s->irq, newirq ? 1 : 0);
}

static void mcp25625_got_reset(MCP25625State *s)
{
    /*
     * most of tx/rx buffs have undefined values after reset,
     * set them to 0 for simplicity and ease of initialisation.
     */
    memset(&s->txbuffs, 0x00, sizeof(s->txbuffs));
    memset(&s->rxbuffs, 0x00, sizeof(s->rxbuffs));
    memset(&s->rxfilters, 0x00, sizeof(s->rxfilters));
    memset(&s->rxmasks, 0x00, sizeof(s->rxmasks));

    /* reset all irqs */
    s->caninte = 0x0;
    s->canintf = 0x0;
    s->lastirq = ~(uint32_t)0x0;
    mcp25625_update_irqs(s, 0x00);
    s->eflg = 0x0;

    s->bfpctrl = 0x0;
    s->txrtsctrl = 0x0;
    s->tec = 0x0;
    s->rec = 0x0;
    memset(&s->cnfs, 0x00, sizeof(s->cnfs));

    /* put controller into config mode now */
    s->canstat = CTRL_REQ_CFG;
    s->canctrl = CTRL_REQ_CFG | CTRL_DEF_CLK;
}

static void mcp25625_send_txb(MCP25625State *s, unsigned buff)
{
    struct txbuff *txb = &s->txbuffs[buff];
    qemu_can_frame frame;
    qemu_canid_t id;
    unsigned len;

    if (!mcp25625_is_in_normal(s) && !mcp25625_is_in_loopback(s)) {
        return;
    }

    len = txb->data[OFF_TXBDLC] & 0xf;
    if (len > 8) {
        len = 8;
    }
    trace_mcp25625_send_txb(trace_name(s), buff, len);

    id = txb->data[OFF_TXBSIDH] << 3;
    id |= (txb->data[OFF_TXBSIDL] & 0xE0) >> 5;

    if (txb->data[OFF_TXBSIDL] & TXBSIDL_EXIDE) {
        id <<= 18;
        id |= QEMU_CAN_EFF_FLAG;
        id |= (qemu_canid_t)(txb->data[OFF_TXBSIDL] & 3) << 16;
        id |= (qemu_canid_t)txb->data[OFF_TXBEID8] << 8;
        id |= (qemu_canid_t)txb->data[OFF_TXBEID0];
    }

    if (txb->data[OFF_TXBDLC] & TXBDLC_RTR) {
        id |= QEMU_CAN_RTR_FLAG;
    }

    memcpy(frame.data, txb->data + OFF_TXBDATA, len);
    frame.can_dlc = len;
    frame.can_id = id;
    frame.flags = 0;

    if (mcp25625_is_in_normal(s)) {
        can_bus_client_send(&s->bus_client, &frame, 1);
    } else {
        mcp25625_can_receive(&s->bus_client, &frame, 1);
    }

    txb->data[OFF_TXBCTRL] &= ~TXBCTRL_TXREQ;
    mcp25625_update_irqs(s, IRQ_TX(buff));
}

static void mcp25625_update_txbctrl(MCP25625State *s, unsigned txbuff)
{
    struct txbuff *txb = &s->txbuffs[txbuff];
    uint8_t txbctrl = txb->data[OFF_TXBCTRL];

    if (txbctrl & TXBCTRL_TXREQ) {
        mcp25625_send_txb(s, txbuff);
    }
}

static void mcp25625_update_canctrl(MCP25625State *s, bool wakeup_happened)
{
    uint8_t ctrl_op, stat_op;
    int txbuff;

    /* check and clear aborted buffers */
    if (s->canctrl & CTRL_ABAT) {
        for (txbuff = 0; txbuff < 2; txbuff++) {
            struct txbuff *txb = &s->txbuffs[txbuff];
            uint8_t *txbctrl = &txb->data[OFF_TXBCTRL];
            if (*txbctrl & TXBCTRL_TXREQ) {
                *txbctrl &= ~TXBCTRL_TXREQ;
                *txbctrl |= TXBCTRL_ABTF;
            }
        }
    }

    ctrl_op = s->canctrl & CTRL_REQ_MASK;
    stat_op = s->canstat & CTRL_REQ_MASK;

    if (!mcp25625_mode_exists(ctrl_op)) {
        ctrl_op = stat_op;
        s->canctrl &= ~CTRL_REQ_MASK;
        s->canctrl |= ctrl_op;
    }

    /* check to see if we should change the device mode */
    if (ctrl_op != stat_op && (wakeup_happened || stat_op != CTRL_REQ_SLEEP)) {
        uint8_t rts = 0;

        trace_mcp25625_change_mode(trace_name(s), stat_op, ctrl_op);

        /* write the new mode to canstat */
        s->canstat &= ~CTRL_REQ_MASK;
        s->canstat |= ctrl_op;

        /* send pending tx if possible */
        for (txbuff = 0; txbuff < 2; txbuff++) {
            struct txbuff *txb = &s->txbuffs[txbuff];
            uint8_t txbctrl = txb->data[OFF_TXBCTRL];

            if (txbctrl & TXBCTRL_TXREQ) {
                rts |= (1 << txbuff);
            }
        }
        mcp25625_do_rts(s, rts);
    }

    /* mcp25625 sets canctrl.reqop to listen mode during sleep */
    if (s->canstat & CTRL_REQ_SLEEP) {
        s->canctrl &= ~CTRL_REQ_MASK;
        s->canctrl |= CTRL_REQ_LISTEN;
    }
}

static void mc25625_update_reg(MCP25625State *s, uint8_t val)
{
    trace_mpc25652_reg_update(trace_name(s), s->ssi_addr, *s->ssi_ptr, val);
    *s->ssi_ptr &= ~s->ssi_writemask;
    *s->ssi_ptr |= val & s->ssi_writemask;

    if (s->ssi_ptr == &s->caninte || s->ssi_ptr == &s->canintf) {
        mcp25625_update_irqs(s, 0x0);
    } else if (s->ssi_ptr == &s->bfpctrl) {
        mcp25625_update_rxbf(s, 0);
        mcp25625_update_rxbf(s, 1);
    } else if (s->ssi_ptr == &s->canctrl) {
        mcp25625_update_canctrl(s, false);
    } else if (s->ssi_ptr == &s->rxbuffs[0].data[OFF_RXBCTRL]) {
        *s->ssi_ptr &= ~RXBCTRL_BUKT1;
        *s->ssi_ptr |= (*s->ssi_ptr & RXBCTRL_BUKT) ? RXBCTRL_BUKT1 : 0;
    } else {
        unsigned txbuff;

        for (txbuff = 0; txbuff <= 2; txbuff++) {
            if (s->ssi_ptr == &s->txbuffs[txbuff].data[OFF_TXBCTRL]) {
                if (s->canctrl & CTRL_ABAT) {
                    *s->ssi_ptr &= ~TXBCTRL_TXREQ;
                } else {
                    *s->ssi_ptr &= ~TXBCTRL_ABTF;
                    mcp25625_update_txbctrl(s, txbuff);
                }
                break;
            }
        }
    }
}

static uint8_t mcp25625_get_txp(MCP25625State *s, int buff)
{
    struct txbuff *txb = &s->txbuffs[buff];
    return txb->data[OFF_TXBCTRL] & TXBCTRL_TXP;
}

static void mcp25625_do_rts(MCP25625State *s, uint32_t tx)
{
    int ordered[3], ordered_used = 0, buf_place;
    int i, curr_buf;

    trace_mcp25625_do_rts(trace_name(s), tx);

    if (s->canctrl & CTRL_ABAT) {
        return;
    }

    for (curr_buf = 2; curr_buf >= 0; curr_buf--) {
        /* skip non-requested bufs */
        if (!(tx & (1 << curr_buf))) {
            continue;
        }

        /* find a place in `ordered` for current buf */
        for (buf_place = 0; buf_place < ordered_used; buf_place++) {
            uint8_t our_txp = mcp25625_get_txp(s, curr_buf);
            uint8_t other_txp = mcp25625_get_txp(s, ordered[buf_place]);

            if (our_txp > other_txp) {
                break;
            }
        }

        /* make room for current buf */
        for (i = ordered_used; i >= buf_place + 1; i--) {
            ordered[i] = ordered[i - 1];
        }

        /* finally insert current buf in `ordered` */
        ordered[buf_place] = curr_buf;
        ordered_used += 1;
    }

    for (i = 0; i < ordered_used; i++) {
        struct txbuff *txb = &s->txbuffs[ordered[i]];
        txb->data[OFF_TXBCTRL] &= ~TXBCTRL_ABTF;
        txb->data[OFF_TXBCTRL] |= TXBCTRL_TXREQ;
        mcp25625_update_txbctrl(s, ordered[i]);
    }
}

static uint32_t mcp25625_get_status(MCP25625State *s)
{
    uint32_t result;

    /* lower two bits map directly to irqs for rx0/rx1 */
    result = (s->canintf & (IRQ_RX0 | IRQ_RX1));

    if (s->canintf & IRQ_TX0) {
        result |= (1 << 3);
    }
    if (s->canintf & IRQ_TX1) {
        result |= (1 << 5);
    }
    if (s->canintf & IRQ_TX2) {
        result |= (1 << 7);
    }

    if (s->txbuffs[0].data[OFF_TXBCTRL] & TXBCTRL_TXREQ) {
        result |= (1 << 2);
    }
    if (s->txbuffs[1].data[OFF_TXBCTRL] & TXBCTRL_TXREQ) {
        result |= (1 << 4);
    }
    if (s->txbuffs[2].data[OFF_TXBCTRL] & TXBCTRL_TXREQ) {
        result |= (1 << 6);
    }

    return result;
}

static uint32_t mcp25625_get_rxstatus(MCP25625State *s)
{
    struct rxbuff *rxbuff = NULL;
    uint32_t result;

    /*
     * [7:6] 0=none, 1=rxb0 full, 2=rxb1 full, 3=both full
     * [4:3] type of frame received (rx0 priority)
     * [2:0] filter hit for frame
     */

    result = (s->canintf & (IRQ_RX0 | IRQ_RX1)) << 6;

    if (s->canintf & IRQ_RX0) {
        rxbuff = &s->rxbuffs[0];
    } else if (s->canintf & IRQ_RX1) {
        rxbuff = &s->rxbuffs[1];
    }

    if (rxbuff) {
        uint8_t rxbctrl = rxbuff->data[OFF_RXBCTRL];

        /* type of frame */
        if (rxbctrl & RXBCTRL_RXRTR) {
            result |= (1 << 3);
        }

        if (rxbuff->data[OFF_RXBSIDL] & RXBSIDL_IDE) {
            result |= (1 << 4);
        }

        /* filter hit depends on buffer */
        if (s->canintf & IRQ_RX0) {
            result |= rxbctrl & 1;
        } else {
            result |= rxbctrl & 7;
            if ((result & 7) <= 1) {
                result += 6;
            }
        }
    }

    return result;
}

static uint32_t mcp25625_transfer8(SSIPeripheral *ss, uint32_t tx)
{
    MCP25625State *s = MCP25625(ss);
    uint32_t ret = 0xffff;

    trace_mcp25625_transfer8(trace_name(s), s->ssi_state, s->ssi_addr, tx);

    switch (s->ssi_state) {
    case SSI_INSTRUCTION:
        s->ssi_write = false;

        /* next bit is in datasheet order, not numerical order */
        if (tx == (3 << 6)) {
            mcp25625_got_reset(s);
            s->ssi_state = SSI_WAIT;
        } else if (tx == 3) {
            s->ssi_state = SSI_ADDRESS;
        } else if ((tx & 1) == 0 && (tx >= 0x90 && tx <= 0x96)) {
            /* read-buffer command */

            switch ((tx - 0x90) >> 1) {
            case 0x0:
                s->ssi_addr = 0x61;
                break;
            case 0x1:
                s->ssi_addr = 0x66;
                break;
            case 0x2:
                s->ssi_addr = 0x71;
                break;
            case 0x3:
                s->ssi_addr = 0x76;
                break;
            }

            s->ssi_state = SSI_RD_DATA;
            s->ssi_rxbuff = (tx - 0x90) >> 2;
        } else if (tx == 2) {           /* write-data instruction */
            s->ssi_state = SSI_ADDRESS;
            s->ssi_write = true;
        } else if (tx >= 0x40 && tx <= 0x45) {
            s->ssi_state = SSI_WR_DATA;
            s->ssi_write = true;

            switch (tx - 0x40) {
            case 0x0:
                s->ssi_addr = 0x31;
                break;
            case 0x1:
                s->ssi_addr = 0x36;
                break;
            case 0x2:
                s->ssi_addr = 0x41;
                break;
            case 0x3:
                s->ssi_addr = 0x46;
                break;
            case 0x4:
                s->ssi_addr = 0x51;
                break;
            case 0x5:
                s->ssi_addr = 0x56;
                break;
            default:
                trace_mcp25625_invalid_cmd(trace_name(s), tx);
                s->ssi_addr = 0x00;
                s->ssi_state = SSI_WAIT;
            }
        } else if (tx >= 0x80 && tx <= 0x87) {
            s->ssi_state = SSI_WAIT;
            mcp25625_do_rts(s, tx);
        } else if (tx == 0xa0) {
            s->ssi_state = SSI_RD_STATUS;
        } else if (tx == 0xb0) {
            s->ssi_state = SSI_RD_RXSTATUS;
        } else if (tx == 5) {
            s->ssi_state = SSI_MODIFY_ADDR;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: spi invalid command %02x\n",
                          trace_name(s), tx);

            trace_mcp25625_invalid_cmd(trace_name(s), tx);
            s->ssi_addr = 0x00;
            s->ssi_state = SSI_WAIT;
        }

        ret = 0xff;
        break;

    case SSI_RD_STATUS:
        ret = mcp25625_get_status(s);
        break;

    case SSI_RD_RXSTATUS:
        ret = mcp25625_get_rxstatus(s);
        break;

    case SSI_WAIT:
        ret = 0xff;
        break;

    case SSI_ADDRESS:
        s->ssi_state = (s->ssi_write) ? SSI_WR_DATA : SSI_RD_DATA;
        s->ssi_addr = tx;
        ret = 0xff;
        break;

    case SSI_WR_DATA:
        s->ssi_ptr = addr_to_reg(s, s->ssi_addr);
        if (s->ssi_ptr) {
            mc25625_update_reg(s, tx);
        }
        s->ssi_addr += 1;
        ret = 0xff;
        break;

    case SSI_RD_DATA:
        s->ssi_ptr = addr_to_reg(s, s->ssi_addr);
        if (s->ssi_only_cfg_rd && !mcp25625_is_in_cfg(s)) {
            ret = 0x00;
        } else if (s->ssi_ptr) {
            ret = *s->ssi_ptr;
        } else {
            ret = 0xff;
        }
        s->ssi_addr += 1;
        break;

    case SSI_MODIFY_ADDR:
        s->ssi_addr = tx;
        s->ssi_ptr = addr_to_reg(s, tx);
        s->ssi_state = SSI_MODIFY_MASK;
        ret = 0xff;
        break;

    case SSI_MODIFY_MASK:
        s->ssi_modify_mask = tx;
        s->ssi_state = SSI_MODIFY_DATA;
        ret = 0xff;
        break;

    case SSI_MODIFY_DATA:
        if (!s->ssi_can_bitmodify) {
            s->ssi_modify_mask = 0xff;
        }
        if (s->ssi_ptr) {
            tx &= s->ssi_modify_mask;
            tx |= *s->ssi_ptr & ~s->ssi_modify_mask;
            mc25625_update_reg(s, tx);
        }

        ret = 0xff;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: spi invalid state (ssi_state=%d)\n",
                      trace_name(s), s->ssi_state);
        s->ssi_state = SSI_WAIT;
        ret = 0xff;
    }

    trace_mcp25625_transfer8_return(trace_name(s), s->ssi_state, s->ssi_addr,
                                    tx, ret);
    return ret;
}

#define client_to_mcp(__c) container_of(__c, MCP25625State, bus_client)

static bool mcp25625_check_filter(const qemu_can_frame *frame,
                                  struct rxmask *mask,
                                  struct rxfilter *filt)
{
    uint32_t mask32, filt32, data;
    bool frame_is_extended = !!(frame->can_id & QEMU_CAN_EFF_FLAG);
    bool filter_is_extended = !!(filt->data[OFF_RXFSIDL] & RXFSIDL_EXIDE);

    if (frame_is_extended != filter_is_extended) {
        return false;
    }

    mask32 =
        (mask->data[OFF_RXMSIDH] << 21)
        | ((mask->data[OFF_RXMSIDL] & 0xE0) << 13)
        | ((mask->data[OFF_RXMSIDL] & 0x3) << 16)
        | (mask->data[OFF_RXMEID8] << 8)
        | (mask->data[OFF_RXMEID0] << 0);

    filt32 =
        (filt->data[OFF_RXFSIDH] << 21)
        | ((filt->data[OFF_RXFSIDL] & 0xE0) << 13)
        | ((filt->data[OFF_RXFSIDL] & 0x3) << 16)
        | (filt->data[OFF_RXFEID8] << 8)
        | (filt->data[OFF_RXFEID0] << 0);

    if (frame_is_extended) {
        data = frame->can_id & QEMU_CAN_EFF_MASK;
    } else {
        /* accept data bytes by default if the frame is too short */
        uint8_t len = (frame->can_id & QEMU_CAN_RTR_FLAG) ? 0 : frame->can_dlc;
        uint8_t data0 = len > 0 ? frame->data[0] : filt->data[OFF_RXFEID8];
        uint8_t data1 = len > 1 ? frame->data[1] : filt->data[OFF_RXFEID0];

        data = (frame->can_id & QEMU_CAN_SFF_MASK) << 18 | data0 << 8 | data1;
        filt32 &= ~(3 << 16);
    }

    return (~mask32 | ~(filt32 ^ data)) == 0xFFFFFFFF;
}

/*
 * mcp25625_check_filters:
 * for rxb0, rxmask0 and rxfilters[0..1] are used
 * for rxb1, rxmask1 and rxfilters[2..5] are used
 */
static int mcp25625_check_filters(MCP25625State *s,
                                  const qemu_can_frame *frame,
                                  unsigned rxb)
{
    struct rxbuff *rxbuff = &s->rxbuffs[rxb];
    uint8_t rxbctrl = rxbuff->data[OFF_RXBCTRL];
    struct rxmask *mask = &s->rxmasks[rxb];
    unsigned nr, end;

    /* if we're not bothering with filters, fake hit on filter 0 */
    if ((rxbctrl & RXBCTRL_RXM_MASK) == RXBCTRL_RXM_ANY) {
        return 0;
    }

    /* note, these are invalid states, so just ignore */
    if ((rxbctrl & RXBCTRL_RXM_MASK) != RXBCTRL_RXM_VALID) {
        return -2;
    }

    if (rxb == 0) {
        nr = 0;
        end = 1;
    } else {
        nr = 2;
        end = 5;
    }

    for (; nr <= end; nr++) {
        if (mcp25625_check_filter(frame, mask, &s->rxfilters[nr])) {
            return nr;
        }
    }

    return -1;
}

static void mcp25625_rx_into_buf(MCP25625State *s,
                                 const qemu_can_frame *frame,
                                 unsigned buffnr, int filthit)
{
    struct rxbuff *rxbuff = &s->rxbuffs[buffnr];
    qemu_canid_t e_id, sidl, id, q_id = frame->can_id;
    unsigned len = frame->can_dlc;

    trace_mcp25625_rx_buf(trace_name(s), buffnr, q_id, len);

    if (q_id & QEMU_CAN_EFF_FLAG) {
        e_id = q_id & QEMU_CAN_EFF_MASK;
        id = e_id >> 18;
    } else {
        id = q_id & QEMU_CAN_SFF_MASK;
        e_id = 0;
    }

    sidl = (id & 0x7) << 5;
    if (q_id & QEMU_CAN_EFF_FLAG) {
        sidl |= RXBSIDL_IDE;
        sidl |= (e_id >> 16) & 3;
    } else {
        if (q_id & QEMU_CAN_RTR_FLAG) {
            sidl |= RXBSIDL_SRR;
        }
    }

    rxbuff->data[OFF_RXBSIDL] = sidl;
    rxbuff->data[OFF_RXBSIDH] = id >> 3;
    rxbuff->data[OFF_RXBEID8] = (e_id >> 8) & 0xff;
    rxbuff->data[OFF_RXBEID0] = e_id & 0xff;

    rxbuff->data[OFF_RXBDLC] = len;
    if ((q_id & QEMU_CAN_RTR_FLAG) && (q_id & QEMU_CAN_EFF_FLAG)) {
        rxbuff->data[OFF_RXBDLC] |= RXBDLC_RTR;
    }

    if (buffnr == 0) {
        rxbuff->data[OFF_RXBCTRL] &= ~((1 << 0) | RXBCTRL_RXRTR);

        if (filthit > 0) {
            rxbuff->data[OFF_RXBCTRL] |= (1 << 0);
        }
    } else {
        rxbuff->data[OFF_RXBCTRL] &= ~(7 | RXBCTRL_RXRTR);
        rxbuff->data[OFF_RXBCTRL] |= filthit;
    }

    if (q_id & QEMU_CAN_RTR_FLAG) {
        rxbuff->data[OFF_RXBCTRL] |= RXBCTRL_RXRTR;
    }

    /* finally copy the frame data in */
    memcpy(rxbuff->data + OFF_TXBDATA, frame->data, len);

    mcp25625_update_irqs(s, IRQ_RX(buffnr));
}

#define is_full(_s, _b) (s->canintf & IRQ_RX(_b))
#define is_bukt(_s) (s->rxbuffs[0].data[OFF_RXBCTRL] & RXBCTRL_BUKT)

static inline void mcp25625_set_eflag(MCP25625State *s, unsigned flag)
{
    s->eflg |= flag;
    mcp25625_update_irqs(s, IRQ_ERR);
}

static ssize_t mcp25625_can_receive(CanBusClientState *client,
                                    const qemu_can_frame *buf,
                                    size_t frames_cnt)
{
    MCP25625State *s = client_to_mcp(client);
    int ret;

    /* support receiving only one frame at a time */
    if (frames_cnt != 1) {
        return -1;
    }

    /* we don't support error frames or buf->flags */
    if (buf->can_id & QEMU_CAN_ERR_FLAG || buf->flags != 0) {
        return -1;
    }

    if (mcp25625_is_in_sleep(s)) {
        mcp25625_update_irqs(s, IRQ_WAKE);
        return 0;
    }

    /* initially, does rxb0 pass this */
    ret = mcp25625_check_filters(s, buf, 0);
    if (ret >= 0) {
        if (is_full(s, 0)) {
            /* if we can roll over into rxb1? */

            if (is_bukt(s)) {
                if (is_full(s, 1)) {
                    /* generate overflow on rxb0. real hw does this */
                    mcp25625_set_eflag(s, EFLG_RX0OVR);
                } else {
                    /* dump this into rxb1 */
                    mcp25625_rx_into_buf(s, buf, 1, ret);
                }
            } else {
                /* generate overflow on rxb0 */
                mcp25625_set_eflag(s, EFLG_RX0OVR);
            }
        } else {
            /* dump this into rxb0 */
            mcp25625_rx_into_buf(s, buf, 0, ret);
        }
    } else {
        /* see if rxb1 will take this */

        ret = mcp25625_check_filters(s, buf, 1);
        if (ret >= 0) {
            if (is_full(s, 1)) {
                /* generate overflow on rxb1 */
                mcp25625_set_eflag(s, EFLG_RX1OVR);
            } else {
                /* dump into rxb1 */
                mcp25625_rx_into_buf(s, buf, 1, ret);
            }
        }
    }

    return 0;
}

static bool mcp25625_can_can_receive(CanBusClientState *client)
{
    MCP25625State *s = client_to_mcp(client);

    return !mcp25625_is_in_cfg(s) && !mcp25625_is_in_loopback(s);
}

static CanBusClientInfo mcp25625_bus_client_info = {
    .can_receive = mcp25625_can_can_receive,
    .receive = mcp25625_can_receive,
};

static void mcp25625_realize(SSIPeripheral *ss, Error **errp)
{
    MCP25625State *s = MCP25625(ss);
    DeviceState *dev = DEVICE(ss);
    int ret;

    s->trace_name = object_get_canonical_path(OBJECT(s));

    if (s->canbus) {
        s->bus_client.info = &mcp25625_bus_client_info;
        ret = can_bus_insert_client(s->canbus, &s->bus_client);
        if (ret) {
            error_setg(errp, "cannot connect mcp25625 to canbus");
            return;
        }
    }

    qdev_init_gpio_out_named(dev, &s->irq, "irq", 1);
    qdev_init_gpio_out_named(dev, &s->rxb_irq[0], "rxbf0", 1);
    qdev_init_gpio_out_named(dev, &s->rxb_irq[1], "rxbf1", 1);
}

static int mcp25625_post_load(void *op, int ver)
{
    MCP25625State *s = op;

    /* setting ssi_ptr also resets other non-saved ssi data */
    s->ssi_ptr = addr_to_reg(s, s->ssi_addr);

    /* reset irq state */
    s->lastirq = ~(uint32_t)0x0;
    mcp25625_update_irqs(s, 0x00);

    return 0;
}

/*
 * Use the mcp25625_got_reset() call to reset the state, which is
 * probably good enough for now.
 */
static void mcp25625_reset(DeviceState *d)
{
    MCP25625State *s = MCP25625(d);

    mcp25625_got_reset(s);
}

static Property mcp25625_properties[] = {
    DEFINE_PROP_LINK("canbus", MCP25625State, canbus, TYPE_CAN_BUS,
                     CanBusState *),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_mcp25625_txbuff = {
    .name = TYPE_MCP25625 "/txbuff",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, struct txbuff, 14),
    },
};

static const VMStateDescription vmstate_mcp25625_rxbuff = {
    .name = TYPE_MCP25625 "/rxbuff",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, struct rxbuff, 14),
    },
};

static const VMStateDescription vmstate_mcp25625_rxmask = {
    .name = TYPE_MCP25625 "/rxmask",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, struct rxmask, 4),
    },
};

static const VMStateDescription vmstate_mcp25625_rxfilter = {
    .name = TYPE_MCP25625 "/rxfilter",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(data, struct rxfilter, 4),
    },
};

static const VMStateDescription vmstate_mcp25625 = {
    .name = TYPE_MCP25625,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = mcp25625_post_load,
    .fields = (VMStateField[]) {
        /*
         *think the ssi transaction state should be store here
         * as not sure if we can suspend/migrate over this
         */
        VMSTATE_BOOL(ssi_write, MCP25625State),
        VMSTATE_UINT32(ssi_state, MCP25625State),
        VMSTATE_UINT8(ssi_addr, MCP25625State),
        VMSTATE_UINT8(ssi_rxbuff, MCP25625State),
        VMSTATE_UINT8(ssi_modify_mask, MCP25625State),
        VMSTATE_UINT8(canstat, MCP25625State),
        VMSTATE_UINT8(canctrl, MCP25625State),
        VMSTATE_UINT8(bfpctrl, MCP25625State),
        VMSTATE_UINT8(txrtsctrl, MCP25625State),
        VMSTATE_UINT8(tec, MCP25625State),
        VMSTATE_UINT8(rec, MCP25625State),
        VMSTATE_UINT8(caninte,  MCP25625State),
        VMSTATE_UINT8(canintf,  MCP25625State),
        VMSTATE_UINT8(eflg,  MCP25625State),
        VMSTATE_UINT8_ARRAY(cnfs, MCP25625State, 4),
        VMSTATE_STRUCT_ARRAY(txbuffs, MCP25625State, 3, 1,
                             vmstate_mcp25625_txbuff, struct txbuff),
        VMSTATE_STRUCT_ARRAY(rxbuffs, MCP25625State, 2, 1,
                             vmstate_mcp25625_rxbuff, struct rxbuff),
        VMSTATE_STRUCT_ARRAY(rxmasks, MCP25625State, 2, 1,
                             vmstate_mcp25625_rxmask, struct rxmask),
        VMSTATE_STRUCT_ARRAY(rxfilters, MCP25625State, 6, 1,
                             vmstate_mcp25625_rxfilter, struct rxfilter),
    },
};

static void mcp25625_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->realize = mcp25625_realize;
    k->transfer = mcp25625_transfer8;
    k->set_cs = mcp25625_cs;
    k->cs_polarity = SSI_CS_LOW;
    dc->vmsd = &vmstate_mcp25625;
    dc->desc = "Microchip MCP25625 CAN-SPI";
    device_class_set_props(dc, mcp25625_properties);
    dc->reset = mcp25625_reset;
}

static const TypeInfo mcp25625_info = {
    .name           = TYPE_MCP25625,
    .parent         = TYPE_SSI_PERIPHERAL,
    .instance_size  = sizeof(MCP25625State),
    .class_init     = mcp25625_class_init,
};

static void mcp25625_register_types(void)
{
    type_register_static(&mcp25625_info);
}

type_init(mcp25625_register_types);
