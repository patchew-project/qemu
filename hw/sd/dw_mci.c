/*
 * Synopsys DesignWare Multimedia Card Interface
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sd/dw_mci.h"

#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "system/dma.h"

REG32(CTRL, 0x000)
    FIELD(CTRL, RESET, 0, 1)
    FIELD(CTRL, FIFO_RESET, 1, 1)
    FIELD(CTRL, DMA_RESET, 2, 1)
    FIELD(CTRL, INT_ENABLE, 4, 1)
    FIELD(CTRL, DMA_ENABLE, 5, 1)
    FIELD(CTRL, USE_IDMAC, 25, 1)
REG32(PWREN, 0x004)
REG32(CLKDIV, 0x008)
REG32(CLKSRC, 0x00c)
REG32(CLKENA, 0x010)
REG32(TMOUT, 0x014)
REG32(CTYPE, 0x018)
REG32(BLKSIZ, 0x01c)
REG32(BYTCNT, 0x020)
REG32(INTMASK, 0x024)
REG32(CMDARG, 0x028)
REG32(CMD, 0x02c)
    FIELD(CMD, INDEX, 0, 6)
    FIELD(CMD, RESP_EXP, 6, 1)
    FIELD(CMD, RESP_LONG, 7, 1)
    FIELD(CMD, RESP_CRC, 8, 1)
    FIELD(CMD, DAT_EXP, 9, 1)
    FIELD(CMD, DAT_WR, 10, 1)
    FIELD(CMD, SEND_STOP, 12, 1)
    FIELD(CMD, STOP, 14, 1)
    FIELD(CMD, UPD_CLK, 21, 1)
    FIELD(CMD, START, 31, 1)
REG32(RESP0, 0x030)
REG32(RESP1, 0x034)
REG32(RESP2, 0x038)
REG32(RESP3, 0x03c)
REG32(MINTSTS, 0x040)
REG32(RINTSTS, 0x044)
REG32(STATUS, 0x048)
    FIELD(STATUS, FIFO_RX_WMARK, 0, 1)
    FIELD(STATUS, FIFO_TX_WMARK, 1, 1)
    FIELD(STATUS, FIFO_EMPTY, 2, 1)
    FIELD(STATUS, FIFO_FULL, 3, 1)
    FIELD(STATUS, BUSY, 9, 1)
    FIELD(STATUS, RESPONSE_INDEX, 11, 6)
    FIELD(STATUS, FIFO_COUNT, 17, 13)
REG32(FIFOTH, 0x04c)
    FIELD(FIFOTH, TX_WMARK, 0, 12)
    FIELD(FIFOTH, RX_WMARK, 16, 12)
    FIELD(FIFOTH, MSIZE, 28, 3)
REG32(CDETECT, 0x050)
REG32(WRTPRT, 0x054)
REG32(GPIO, 0x058)
REG32(TCBCNT, 0x05c)
REG32(TBBCNT, 0x060)
REG32(DEBNCE, 0x064)
REG32(USRID, 0x068)
REG32(VERID, 0x06c)
REG32(HCON, 0x070)
    FIELD(HCON, CARD_TYPE, 0, 1)
    FIELD(HCON, NUM_CARDS, 1, 5)
    FIELD(HCON, HBUS_TYPE, 6, 1)
    FIELD(HCON, HDATA_WIDTH, 7, 3)
    FIELD(HCON, HADDR_WIDTH, 10, 6)
    FIELD(HCON, TRANS_MODE, 16, 2)
    FIELD(HCON, FIFO_RAM_INSIDE, 21, 1)
    FIELD(HCON, HOLD_REG, 22, 1)
    /* Later revisions use bit 27 to advertise 64-bit IDMAC addresses */
    FIELD(HCON, ADDR_CONFIG, 27, 1)
REG32(UHS_REG, 0x074)
REG32(RST_N, 0x078)
REG32(BMOD, 0x080)
    FIELD(BMOD, SWRESET, 0, 1)
    FIELD(BMOD, FIXED_BURST, 1, 1)
    FIELD(BMOD, DSL, 2, 5)
    FIELD(BMOD, ENABLE, 7, 1)
    FIELD(BMOD, PBL, 8, 3)
REG32(PLDMND, 0x084)
REG32(IDMAC_088, 0x088)
REG32(IDMAC_08C, 0x08c)
REG32(IDMAC_090, 0x090)
REG32(IDMAC_094, 0x094)
REG32(IDMAC_098, 0x098)
REG32(IDMAC_09C, 0x09c)
REG32(IDMAC_0A0, 0x0a0)
REG32(IDMAC_0A4, 0x0a4)
REG32(CDTHRCTL, 0x100)
REG32(BACK_END_POWER, 0x104)
REG32(UHS_REG_EXT, 0x108)
REG32(DDR_REG, 0x10c)
REG32(ENABLE_SHIFT, 0x110)

#define DW_MCI_INT_CD               BIT(0)
#define DW_MCI_INT_RESP_ERR         BIT(1)
#define DW_MCI_INT_CMD_DONE         BIT(2)
#define DW_MCI_INT_DATA_OVER        BIT(3)
#define DW_MCI_INT_TXDR             BIT(4)
#define DW_MCI_INT_RXDR             BIT(5)
#define DW_MCI_INT_RCRC             BIT(6)
#define DW_MCI_INT_DCRC             BIT(7)
#define DW_MCI_INT_RTO              BIT(8)
#define DW_MCI_INT_DRTO             BIT(9)
#define DW_MCI_INT_HTO              BIT(10)
#define DW_MCI_INT_FRUN             BIT(11)
#define DW_MCI_INT_HLE              BIT(12)
#define DW_MCI_INT_SBE              BIT(13)
#define DW_MCI_INT_ACD              BIT(14)
#define DW_MCI_INT_EBE              BIT(15)

#define DW_MCI_IDSTS_TI             BIT(0)
#define DW_MCI_IDSTS_RI             BIT(1)
#define DW_MCI_IDSTS_FBE            BIT(2)
#define DW_MCI_IDSTS_DU             BIT(4)
#define DW_MCI_IDSTS_CES            BIT(5)
#define DW_MCI_IDSTS_NI             BIT(8)
#define DW_MCI_IDSTS_AI             BIT(9)
#define DW_MCI_IDSTS_NORMAL         (DW_MCI_IDSTS_TI | DW_MCI_IDSTS_RI)
#define DW_MCI_IDSTS_ABNORMAL       (DW_MCI_IDSTS_FBE | DW_MCI_IDSTS_DU | \
                                     DW_MCI_IDSTS_CES)
#define DW_MCI_IDSTS_MASK           (DW_MCI_IDSTS_TI | DW_MCI_IDSTS_RI | \
                                     DW_MCI_IDSTS_FBE | DW_MCI_IDSTS_DU | \
                                     DW_MCI_IDSTS_CES | DW_MCI_IDSTS_NI | \
                                     DW_MCI_IDSTS_AI)

#define DW_MCI_IDMAC_DIC            BIT(1)
#define DW_MCI_IDMAC_LD             BIT(2)
#define DW_MCI_IDMAC_FS             BIT(3)
#define DW_MCI_IDMAC_CH             BIT(4)
#define DW_MCI_IDMAC_ER             BIT(5)
#define DW_MCI_IDMAC_CES            BIT(30)
#define DW_MCI_IDMAC_OWN            BIT(31)
#define DW_MCI_IDMAC_SIZE_MASK      0x1fff
#define DW_MCI_IDMAC_SIZE2_SHIFT    13
#define DW_MCI_IDMAC_MAX_DESCS      4096
#define DW_MCI_DMA_CHUNK_SIZE       1024

#define DW_MCI_IDMAC_FSM_IDLE       0
#define DW_MCI_IDMAC_FSM_SUSPEND    1

#define TYPE_DW_MCI_BUS "dw-mci-bus"

typedef struct DwMciIdmac32 {
    uint32_t control;
    uint32_t sizes;
    uint32_t buffer;
    uint32_t next;
} DwMciIdmac32;

typedef struct DwMciIdmac64 {
    uint32_t control;
    uint32_t reserved0;
    uint32_t sizes;
    uint32_t reserved1;
    uint32_t buffer_lo;
    uint32_t buffer_hi;
    uint32_t next_lo;
    uint32_t next_hi;
} DwMciIdmac64;

typedef struct DwMciDescriptor {
    uint32_t control;
    uint32_t size[2];
    uint64_t buffer[2];
    uint64_t next;
} DwMciDescriptor;

static bool dw_mci_run_idmac(DwMciState *s);

static bool dw_mci_is_64bit(DwMciState *s)
{
    return s->hcon & R_HCON_ADDR_CONFIG_MASK;
}

/*
 * The 64-bit layout inserts DBADDRU at 0x8c, shifting IDSTS and IDINTEN one
 * register higher than their locations in the legacy layout.
 */
static unsigned dw_mci_idsts_reg(DwMciState *s)
{
    return dw_mci_is_64bit(s) ? R_IDMAC_090 : R_IDMAC_08C;
}

static unsigned dw_mci_idinten_reg(DwMciState *s)
{
    return dw_mci_is_64bit(s) ? R_IDMAC_094 : R_IDMAC_090;
}

static uint32_t dw_mci_host_bus_bytes(DwMciState *s)
{
    unsigned width = FIELD_EX32(s->hcon, HCON, HDATA_WIDTH);

    return width <= 2 ? 2U << width : sizeof(uint32_t);
}

static void dw_mci_set_idmac_fsm(DwMciState *s, uint32_t state)
{
    unsigned idsts = dw_mci_idsts_reg(s);

    s->regs[idsts] = deposit32(s->regs[idsts], 13, 4, state);
}

static uint64_t dw_mci_descriptor_base(DwMciState *s)
{
    uint64_t address = s->regs[R_IDMAC_088];

    if (dw_mci_is_64bit(s)) {
        address |= (uint64_t)s->regs[R_IDMAC_08C] << 32;
    }
    return address;
}

static uint32_t dw_mci_fifo_capacity(DwMciState *s)
{
    return MIN(s->fifo_depth, (uint32_t)DW_MCI_FIFO_MAX_WORDS) * 4;
}

static void dw_mci_fifo_reset(DwMciState *s)
{
    s->fifo_head = 0;
    s->fifo_len = 0;
    memset(s->fifo, 0, sizeof(s->fifo));
}

static void dw_mci_fifo_push(DwMciState *s, const uint8_t *data,
                             uint32_t length)
{
    uint32_t capacity = dw_mci_fifo_capacity(s);
    uint32_t tail = (s->fifo_head + s->fifo_len) % capacity;

    while (length--) {
        s->fifo[tail] = *data++;
        tail = (tail + 1) % capacity;
        s->fifo_len++;
    }
}

static void dw_mci_fifo_pop(DwMciState *s, uint8_t *data, uint32_t length)
{
    uint32_t capacity = dw_mci_fifo_capacity(s);

    while (length--) {
        *data++ = s->fifo[s->fifo_head];
        s->fifo_head = (s->fifo_head + 1) % capacity;
        s->fifo_len--;
    }
    if (!s->fifo_len) {
        s->fifo_head = 0;
    }
}

static void dw_mci_update_irq(DwMciState *s)
{
    uint32_t core = s->regs[R_RINTSTS] & s->regs[R_INTMASK];
    uint32_t idsts = s->regs[dw_mci_idsts_reg(s)];
    uint32_t idinten = s->regs[dw_mci_idinten_reg(s)];
    bool core_irq = (s->regs[R_CTRL] & R_CTRL_INT_ENABLE_MASK) && core;
    bool idmac_irq = ((idsts & DW_MCI_IDSTS_NI) &&
                      (idinten & DW_MCI_IDSTS_NI)) ||
                     ((idsts & DW_MCI_IDSTS_AI) &&
                      (idinten & DW_MCI_IDSTS_AI));

    /* The top-level interrupt is the logical OR of the BIU and IDMAC lines */
    qemu_set_irq(s->irq, core_irq || idmac_irq);
}

static void dw_mci_set_idmac_status(DwMciState *s, uint32_t status)
{
    unsigned idsts = dw_mci_idsts_reg(s);
    uint32_t idinten = s->regs[dw_mci_idinten_reg(s)];

    s->regs[idsts] |= status;

    /*
     * NIS and AIS are sticky summary events, not aliases of the individual
     * status bits.  The databook requires both the member interrupt and its
     * group interrupt to be enabled before the summary is raised.
     */
    if ((status & DW_MCI_IDSTS_NORMAL & idinten) &&
        (idinten & DW_MCI_IDSTS_NI)) {
        s->regs[idsts] |= DW_MCI_IDSTS_NI;
    }
    if ((status & DW_MCI_IDSTS_ABNORMAL & idinten) &&
        (idinten & DW_MCI_IDSTS_AI)) {
        s->regs[idsts] |= DW_MCI_IDSTS_AI;
    }
}

static void dw_mci_update_fifo_interrupts(DwMciState *s)
{
    uint32_t fifo_words = DIV_ROUND_UP(s->fifo_len, sizeof(uint32_t));
    uint32_t tx_wmark = FIELD_EX32(s->regs[R_FIFOTH], FIFOTH, TX_WMARK);
    uint32_t rx_wmark = FIELD_EX32(s->regs[R_FIFOTH], FIFOTH, RX_WMARK);

    if (!s->transfer_active) {
        s->regs[R_RINTSTS] &= ~(DW_MCI_INT_RXDR | DW_MCI_INT_TXDR);
    } else if (s->transfer_write) {
        if (fifo_words <= tx_wmark) {
            s->regs[R_RINTSTS] |= DW_MCI_INT_TXDR;
        } else {
            s->regs[R_RINTSTS] &= ~DW_MCI_INT_TXDR;
        }
        s->regs[R_RINTSTS] &= ~DW_MCI_INT_RXDR;
    } else {
        if (fifo_words > rx_wmark) {
            s->regs[R_RINTSTS] |= DW_MCI_INT_RXDR;
        } else {
            s->regs[R_RINTSTS] &= ~DW_MCI_INT_RXDR;
        }
        s->regs[R_RINTSTS] &= ~DW_MCI_INT_TXDR;
    }
    dw_mci_update_irq(s);
}

static void dw_mci_send_auto_stop(DwMciState *s)
{
    SDRequest request = { .cmd = 12, .arg = 0 };
    uint8_t response[16];
    size_t response_len;

    if (!s->transfer_send_stop) {
        return;
    }
    s->transfer_send_stop = false;
    response_len = sdbus_do_command(&s->sdbus, &request, response,
                                    sizeof(response));
    if (response_len == sizeof(uint32_t)) {
        /* Auto-stop preserves the preceding response in RESP0 */
        s->regs[R_RESP1] = ldl_be_p(response);
        s->regs[R_STATUS] = FIELD_DP32(s->regs[R_STATUS], STATUS,
                                       RESPONSE_INDEX, request.cmd);
    }
    s->regs[R_RINTSTS] |= DW_MCI_INT_ACD;
}

static void dw_mci_complete_data(DwMciState *s)
{
    dw_mci_send_auto_stop(s);
    s->regs[R_RINTSTS] |= DW_MCI_INT_DATA_OVER;
    s->transfer_active = false;
    s->transfer_remaining = 0;
    dw_mci_update_fifo_interrupts(s);
}

static void dw_mci_set_data_error(DwMciState *s)
{
    s->regs[R_RINTSTS] |= DW_MCI_INT_DRTO;
    s->transfer_active = false;
    dw_mci_update_fifo_interrupts(s);
}

static void dw_mci_refill_fifo(DwMciState *s)
{
    uint8_t buffer[DW_MCI_DMA_CHUNK_SIZE];
    uint32_t capacity = dw_mci_fifo_capacity(s);

    while (s->transfer_active && !s->transfer_write &&
           s->transfer_remaining && s->fifo_len < capacity) {
        uint32_t length = MIN((uint32_t)sizeof(buffer),
                              s->transfer_remaining);

        length = MIN(length, capacity - s->fifo_len);
        sdbus_read_data(&s->sdbus, buffer, length);
        dw_mci_fifo_push(s, buffer, length);
        s->transfer_remaining -= length;
        s->regs[R_TCBCNT] += length;
    }
    if (!s->transfer_remaining) {
        /*
         * DATA_OVER follows completion at the card side. Keep the transfer
         * active until the guest drains prefetched data so FIFO status still
         * describes the readable bytes.
         */
        s->regs[R_RINTSTS] |= DW_MCI_INT_DATA_OVER;
    }
    dw_mci_update_fifo_interrupts(s);
}

static void dw_mci_store_response(DwMciState *s, const uint8_t *response,
                                  size_t length)
{
    s->regs[R_RESP0] = 0;
    s->regs[R_RESP1] = 0;
    s->regs[R_RESP2] = 0;
    s->regs[R_RESP3] = 0;

    if (length == 4) {
        s->regs[R_RESP0] = ldl_be_p(response);
    } else if (length == 16) {
        /*
         * SDBus returns a long response most-significant word first, while
         * the controller exposes its least-significant word in RESP0.
         */
        s->regs[R_RESP0] = ldl_be_p(response + 12);
        s->regs[R_RESP1] = ldl_be_p(response + 8);
        s->regs[R_RESP2] = ldl_be_p(response + 4);
        s->regs[R_RESP3] = ldl_be_p(response);
    }
}

static bool dw_mci_idmac_armed(DwMciState *s)
{
    return (s->regs[R_CTRL] & R_CTRL_USE_IDMAC_MASK) &&
           (s->regs[R_BMOD] & R_BMOD_ENABLE_MASK) &&
           FIELD_EX32(s->hcon, HCON, TRANS_MODE) == 0 &&
           !s->idmac_fatal;
}

static bool dw_mci_send_command(DwMciState *s, uint32_t command)
{
    SDRequest request = { 0 };
    uint8_t response[16];
    bool response_expected = command & R_CMD_RESP_EXP_MASK;
    bool long_response = command & R_CMD_RESP_LONG_MASK;
    size_t response_len;

    s->regs[R_CMD] = command & ~R_CMD_START_MASK;

    if (command & R_CMD_UPD_CLK_MASK) {
        /* No card command is sent, so the CIU must not raise Command Done */
        return true;
    }

    if (!sdbus_get_inserted(&s->sdbus) && response_expected) {
        s->regs[R_RINTSTS] |= DW_MCI_INT_CMD_DONE | DW_MCI_INT_RTO;
        dw_mci_update_irq(s);
        return false;
    }

    request.cmd = FIELD_EX32(command, CMD, INDEX);
    request.arg = s->regs[R_CMDARG];
    response_len = sdbus_do_command(&s->sdbus, &request, response,
                                    sizeof(response));

    if (response_expected &&
        ((!long_response && response_len != 4) ||
         (long_response && response_len != 16))) {
        s->regs[R_RINTSTS] |= DW_MCI_INT_CMD_DONE | DW_MCI_INT_RTO;
        dw_mci_update_irq(s);
        return false;
    }
    if (response_expected) {
        dw_mci_store_response(s, response, response_len);
        s->regs[R_STATUS] = FIELD_DP32(s->regs[R_STATUS], STATUS,
                                       RESPONSE_INDEX, request.cmd);
    }

    s->regs[R_RINTSTS] |= DW_MCI_INT_CMD_DONE;

    if (command & R_CMD_STOP_MASK) {
        s->transfer_active = false;
        s->transfer_remaining = 0;
        s->transfer_send_stop = false;
        dw_mci_update_fifo_interrupts(s);
        return true;
    }

    if (command & R_CMD_DAT_EXP_MASK) {
        if (!sdbus_get_inserted(&s->sdbus) || !s->regs[R_BYTCNT]) {
            dw_mci_set_data_error(s);
            return false;
        }
        dw_mci_fifo_reset(s);
        s->regs[R_TCBCNT] = 0;
        s->regs[R_TBBCNT] = 0;
        s->transfer_remaining = s->regs[R_BYTCNT];
        s->transfer_active = true;
        s->transfer_write = command & R_CMD_DAT_WR_MASK;
        s->transfer_send_stop = command & R_CMD_SEND_STOP_MASK;
        s->idmac_desc_addr = dw_mci_descriptor_base(s);
        s->idmac_suspended = false;
        dw_mci_set_idmac_fsm(s, DW_MCI_IDMAC_FSM_IDLE);

        if (dw_mci_idmac_armed(s)) {
            dw_mci_run_idmac(s);
        } else if (s->transfer_write) {
            dw_mci_update_fifo_interrupts(s);
        } else {
            dw_mci_refill_fifo(s);
        }
    }

    dw_mci_update_irq(s);
    return true;
}

static void dw_mci_set_current_descriptor(DwMciState *s,
                                          uint64_t descriptor,
                                          uint64_t buffer)
{
    if (dw_mci_is_64bit(s)) {
        s->regs[R_IDMAC_098] = descriptor;
        s->regs[R_IDMAC_09C] = descriptor >> 32;
        s->regs[R_IDMAC_0A0] = buffer;
        s->regs[R_IDMAC_0A4] = buffer >> 32;
    } else {
        s->regs[R_IDMAC_094] = descriptor;
        s->regs[R_IDMAC_098] = buffer;
    }
}

static bool dw_mci_read_descriptor32(uint64_t address,
                                     DwMciDescriptor *result)
{
    DwMciIdmac32 descriptor;
    MemTxResult tx_result;

    tx_result = dma_memory_read(&address_space_memory, address, &descriptor,
                                sizeof(descriptor), MEMTXATTRS_UNSPECIFIED);
    if (tx_result != MEMTX_OK) {
        return false;
    }

    result->control = le32_to_cpu(descriptor.control);
    result->size[0] = le32_to_cpu(descriptor.sizes) &
                      DW_MCI_IDMAC_SIZE_MASK;
    result->size[1] = (le32_to_cpu(descriptor.sizes) >>
                       DW_MCI_IDMAC_SIZE2_SHIFT) & DW_MCI_IDMAC_SIZE_MASK;
    result->buffer[0] = le32_to_cpu(descriptor.buffer);
    result->buffer[1] = le32_to_cpu(descriptor.next);
    result->next = result->buffer[1];
    return true;
}

static bool dw_mci_read_descriptor64(uint64_t address,
                                     DwMciDescriptor *result)
{
    DwMciIdmac64 descriptor;
    MemTxResult tx_result;

    tx_result = dma_memory_read(&address_space_memory, address, &descriptor,
                                sizeof(descriptor), MEMTXATTRS_UNSPECIFIED);
    if (tx_result != MEMTX_OK) {
        return false;
    }

    result->control = le32_to_cpu(descriptor.control);
    result->size[0] = le32_to_cpu(descriptor.sizes) &
                      DW_MCI_IDMAC_SIZE_MASK;
    result->size[1] = 0;
    result->buffer[0] = le32_to_cpu(descriptor.buffer_lo) |
                        (uint64_t)le32_to_cpu(descriptor.buffer_hi) << 32;
    result->buffer[1] = 0;
    result->next = le32_to_cpu(descriptor.next_lo) |
                   (uint64_t)le32_to_cpu(descriptor.next_hi) << 32;
    return true;
}

static bool dw_mci_write_descriptor_control(uint64_t address,
                                            uint32_t control)
{
    uint32_t value = cpu_to_le32(control);

    return dma_memory_write(&address_space_memory, address, &value,
                            sizeof(value), MEMTXATTRS_UNSPECIFIED) == MEMTX_OK;
}

static bool dw_mci_transfer_buffer(DwMciState *s, uint64_t address,
                                   uint32_t length)
{
    uint8_t buffer[DW_MCI_DMA_CHUNK_SIZE];

    while (length) {
        uint32_t chunk = MIN(length, (uint32_t)sizeof(buffer));
        MemTxResult result;

        if (s->transfer_write) {
            result = dma_memory_read(&address_space_memory, address, buffer,
                                     chunk, MEMTXATTRS_UNSPECIFIED);
            if (result != MEMTX_OK) {
                return false;
            }
            sdbus_write_data(&s->sdbus, buffer, chunk);
        } else {
            sdbus_read_data(&s->sdbus, buffer, chunk);
            result = dma_memory_write(&address_space_memory, address, buffer,
                                      chunk, MEMTXATTRS_UNSPECIFIED);
            if (result != MEMTX_OK) {
                return false;
            }
        }

        address += chunk;
        length -= chunk;
        s->transfer_remaining -= chunk;
        s->regs[R_TCBCNT] += chunk;
        s->regs[R_TBBCNT] += chunk;
    }
    return true;
}

static void dw_mci_suspend_idmac(DwMciState *s)
{
    s->idmac_suspended = true;
    dw_mci_set_idmac_fsm(s, DW_MCI_IDMAC_FSM_SUSPEND);
    dw_mci_set_idmac_status(s, DW_MCI_IDSTS_DU);
    dw_mci_update_irq(s);
}

static void dw_mci_fatal_idmac_error(DwMciState *s)
{
    s->idmac_fatal = true;
    s->idmac_suspended = false;
    s->transfer_active = false;
    dw_mci_set_idmac_fsm(s, DW_MCI_IDMAC_FSM_IDLE);
    dw_mci_set_idmac_status(s, DW_MCI_IDSTS_FBE);
    dw_mci_update_fifo_interrupts(s);
}

static uint64_t dw_mci_next_descriptor(DwMciState *s,
                                       const DwMciDescriptor *descriptor,
                                       uint64_t current,
                                       uint32_t descriptor_size)
{
    if (descriptor->control & DW_MCI_IDMAC_CH) {
        return descriptor->next &
               ~(uint64_t)(dw_mci_host_bus_bytes(s) - 1);
    }
    if (descriptor->control & DW_MCI_IDMAC_ER) {
        return dw_mci_descriptor_base(s);
    }

    /* DSL is expressed in host-bus-width units, not bytes */
    return current + descriptor_size +
           FIELD_EX32(s->regs[R_BMOD], BMOD, DSL) *
           dw_mci_host_bus_bytes(s);
}

static bool dw_mci_run_idmac(DwMciState *s)
{
    uint32_t descriptor_size = dw_mci_is_64bit(s) ?
                               sizeof(DwMciIdmac64) :
                               sizeof(DwMciIdmac32);
    unsigned int count = 0;

    if (!s->transfer_active || !dw_mci_idmac_armed(s)) {
        return false;
    }
    if (!s->idmac_desc_addr) {
        s->idmac_desc_addr = dw_mci_descriptor_base(s);
    }
    if (!s->idmac_desc_addr) {
        dw_mci_suspend_idmac(s);
        return false;
    }

    /*
     * Bound descriptor traversal so a cyclic guest chain cannot monopolize
     * the vCPU. Exhaustion follows the descriptor-unavailable suspend path
     * below, allowing the guest to recover through PLDMND.
     */
    while (s->transfer_remaining && count++ < DW_MCI_IDMAC_MAX_DESCS) {
        DwMciDescriptor descriptor = { 0 };
        uint64_t descriptor_address = s->idmac_desc_addr;
        bool read_ok;
        unsigned int i;

        dw_mci_set_current_descriptor(s, descriptor_address, 0);
        if (dw_mci_is_64bit(s)) {
            read_ok = dw_mci_read_descriptor64(descriptor_address,
                                               &descriptor);
        } else {
            read_ok = dw_mci_read_descriptor32(descriptor_address,
                                               &descriptor);
        }
        if (!read_ok) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: failed to read IDMAC descriptor at "
                          "0x%" PRIx64 "\n",
                          TYPE_DW_MCI, descriptor_address);
            dw_mci_fatal_idmac_error(s);
            return false;
        }
        if (!(descriptor.control & DW_MCI_IDMAC_OWN)) {
            dw_mci_suspend_idmac(s);
            return false;
        }

        for (i = 0; i < ARRAY_SIZE(descriptor.buffer) &&
                    s->transfer_remaining; i++) {
            uint64_t buffer_address;
            uint32_t length = MIN(descriptor.size[i],
                                  s->transfer_remaining);

            /* DES3 is a next-descriptor pointer in chained mode */
            if (i == 1 && (descriptor.control & DW_MCI_IDMAC_CH)) {
                break;
            }
            if (!length) {
                continue;
            }
            buffer_address = descriptor.buffer[i] &
                             ~(uint64_t)(dw_mci_host_bus_bytes(s) - 1);
            dw_mci_set_current_descriptor(s, descriptor_address,
                                          buffer_address);
            if (!dw_mci_transfer_buffer(s, buffer_address, length)) {
                dw_mci_fatal_idmac_error(s);
                return false;
            }
        }

        descriptor.control &= ~DW_MCI_IDMAC_OWN;
        if (!dw_mci_write_descriptor_control(descriptor_address,
                                             descriptor.control)) {
            dw_mci_fatal_idmac_error(s);
            return false;
        }

        if (!(descriptor.control & DW_MCI_IDMAC_DIC)) {
            dw_mci_set_idmac_status(s, s->transfer_write ?
                                    DW_MCI_IDSTS_TI : DW_MCI_IDSTS_RI);
        }

        if (!s->transfer_remaining) {
            break;
        }
        if (descriptor.control & DW_MCI_IDMAC_LD) {
            dw_mci_suspend_idmac(s);
            return false;
        }

        s->idmac_desc_addr = dw_mci_next_descriptor(s, &descriptor,
                                                    descriptor_address,
                                                    descriptor_size);
        if (!s->idmac_desc_addr) {
            dw_mci_suspend_idmac(s);
            return false;
        }
    }

    if (s->transfer_remaining) {
        dw_mci_suspend_idmac(s);
        return false;
    }

    s->idmac_suspended = false;
    dw_mci_set_idmac_fsm(s, DW_MCI_IDMAC_FSM_IDLE);
    dw_mci_complete_data(s);
    dw_mci_update_irq(s);
    return true;
}

static uint64_t dw_mci_read_data(DwMciState *s, unsigned size)
{
    uint8_t buffer[sizeof(uint64_t)] = { 0 };
    uint32_t length;

    if (!s->transfer_active || s->transfer_write) {
        return 0;
    }
    if (!s->fifo_len && s->transfer_remaining) {
        dw_mci_refill_fifo(s);
    }

    length = MIN((uint32_t)size, s->fifo_len);
    if (length) {
        dw_mci_fifo_pop(s, buffer, length);
        s->regs[R_TBBCNT] += length;
    }
    if (s->transfer_remaining) {
        dw_mci_refill_fifo(s);
    }
    if (!s->transfer_remaining && !s->fifo_len) {
        dw_mci_complete_data(s);
    } else {
        dw_mci_update_fifo_interrupts(s);
    }
    return ldn_le_p(buffer, size);
}

static void dw_mci_write_data(DwMciState *s, uint64_t value, unsigned size)
{
    uint8_t buffer[sizeof(uint64_t)] = { 0 };
    uint32_t length;

    if (!s->transfer_active || !s->transfer_write) {
        return;
    }

    /*
     * The functional model consumes PIO writes synchronously through SDBus.
     * Its FIFO therefore remains empty and TXDR continues to advertise room.
     */
    length = MIN((uint32_t)size, s->transfer_remaining);
    stn_le_p(buffer, size, value);
    sdbus_write_data(&s->sdbus, buffer, length);
    s->transfer_remaining -= length;
    s->regs[R_TCBCNT] += length;
    s->regs[R_TBBCNT] += length;

    if (!s->transfer_remaining) {
        dw_mci_complete_data(s);
    } else {
        dw_mci_update_fifo_interrupts(s);
    }
}

static bool dw_mci_idmac_offset(hwaddr offset)
{
    return offset >= A_IDMAC_088 && offset <= A_IDMAC_0A4 &&
           !(offset & 3);
}

static uint64_t dw_mci_read_idmac_register(DwMciState *s, hwaddr offset)
{
    unsigned reg = offset / sizeof(uint32_t);

    if (!dw_mci_is_64bit(s) && offset > A_IDMAC_098) {
        return 0;
    }
    return s->regs[reg];
}

static void dw_mci_write_idmac_register(DwMciState *s, hwaddr offset,
                                        uint32_t value)
{
    unsigned reg = offset / sizeof(uint32_t);
    unsigned idsts = dw_mci_idsts_reg(s);
    unsigned idinten = dw_mci_idinten_reg(s);

    if (reg == idsts) {
        s->regs[reg] &= ~(value & DW_MCI_IDSTS_MASK);
    } else if (reg == idinten) {
        s->regs[reg] = value & DW_MCI_IDSTS_MASK;
    } else if (offset == A_IDMAC_088) {
        s->regs[reg] = value & ~(dw_mci_host_bus_bytes(s) - 1);
    } else if (dw_mci_is_64bit(s) && offset == A_IDMAC_08C) {
        s->regs[reg] = value;
    }
    dw_mci_update_irq(s);
}

static void dw_mci_reset_idmac(DwMciState *s)
{
    s->regs[R_BMOD] = 0;
    s->regs[R_IDMAC_088] = 0;
    if (dw_mci_is_64bit(s)) {
        s->regs[R_IDMAC_08C] = 0;
        s->regs[R_IDMAC_090] = 0;
        s->regs[R_IDMAC_094] = 0;
        s->regs[R_IDMAC_098] = 0;
        s->regs[R_IDMAC_09C] = 0;
        s->regs[R_IDMAC_0A0] = 0;
        s->regs[R_IDMAC_0A4] = 0;
    } else {
        s->regs[R_IDMAC_08C] = 0;
        s->regs[R_IDMAC_090] = 0;
        s->regs[R_IDMAC_094] = 0;
        s->regs[R_IDMAC_098] = 0;
    }
    s->idmac_desc_addr = 0;
    s->idmac_suspended = false;
    s->idmac_fatal = false;
}

static uint64_t dw_mci_ctrl_pre_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    if (value & R_CTRL_RESET_MASK) {
        /*
         * CTRL.controller_reset only resets the BIU/CIU interface and its
         * state machines.  In particular, it must not clear registers, FIFO
         * contents, interrupt state, or the IDMAC CSR block.
         */
        s->transfer_active = false;
        s->transfer_remaining = 0;
        s->transfer_send_stop = false;
        value &= ~(BIT(6) | BIT(7) | BIT(8));
    }
    if (value & R_CTRL_FIFO_RESET_MASK) {
        dw_mci_fifo_reset(s);
    }
    /* CTRL.dma_reset belongs to the external DMA handshake, not IDMAC */
    return value & ~(R_CTRL_RESET_MASK | R_CTRL_FIFO_RESET_MASK |
                     R_CTRL_DMA_RESET_MASK);
}

static void dw_mci_ctrl_post_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    dw_mci_update_irq(s);
}

static void dw_mci_pwren_post_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    if (value & 1) {
        sdbus_set_voltage(&s->sdbus, SD_VOLTAGE_3_3V);
    }
}

static uint64_t dw_mci_cmd_pre_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);
    uint32_t command = value;

    if (command & R_CMD_START_MASK) {
        dw_mci_send_command(s, command);
    }
    return command & ~R_CMD_START_MASK;
}

static void dw_mci_status_post_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    dw_mci_update_fifo_interrupts(s);
}

static uint64_t dw_mci_mintsts_post_read(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    return s->regs[R_RINTSTS] & s->regs[R_INTMASK];
}

static uint64_t dw_mci_status_post_read(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);
    uint32_t capacity = dw_mci_fifo_capacity(s);
    uint32_t fifo_words = DIV_ROUND_UP(s->fifo_len, sizeof(uint32_t));
    uint32_t tx_wmark = FIELD_EX32(s->regs[R_FIFOTH], FIFOTH, TX_WMARK);
    uint32_t rx_wmark = FIELD_EX32(s->regs[R_FIFOTH], FIFOTH, RX_WMARK);
    uint32_t status = s->regs[R_STATUS] & R_STATUS_RESPONSE_INDEX_MASK;

    status = FIELD_DP32(status, STATUS, FIFO_COUNT, fifo_words);
    if (!s->fifo_len) {
        status |= R_STATUS_FIFO_EMPTY_MASK;
    }
    if (s->fifo_len >= capacity) {
        status |= R_STATUS_FIFO_FULL_MASK;
    }
    if (fifo_words > rx_wmark) {
        status |= R_STATUS_FIFO_RX_WMARK_MASK;
    }
    if (fifo_words <= tx_wmark) {
        status |= R_STATUS_FIFO_TX_WMARK_MASK;
    }
    if (s->transfer_active) {
        status |= R_STATUS_BUSY_MASK;
    }
    return status;
}

static uint64_t dw_mci_cdetect_post_read(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    return sdbus_get_inserted(&s->sdbus) ? 0 : 1;
}

static uint64_t dw_mci_wrtprt_post_read(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    return sdbus_get_readonly(&s->sdbus) ? 1 : 0;
}

static uint64_t dw_mci_verid_post_read(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    return s->verid;
}

static uint64_t dw_mci_hcon_post_read(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    return s->hcon;
}

static uint64_t dw_mci_bmod_pre_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    if (value & R_BMOD_SWRESET_MASK) {
        uint32_t pbl = FIELD_EX32(s->regs[R_FIFOTH], FIFOTH, MSIZE);

        dw_mci_reset_idmac(s);
        /* The read-only PBL mirror is not reset with the IDMAC state */
        s->regs[R_BMOD] = FIELD_DP32(0, BMOD, PBL, pbl);
        return s->regs[R_BMOD];
    }

    /* PBL is a read-only mirror of FIFOTH.MSIZE */
    return FIELD_DP32(value, BMOD, PBL,
                      FIELD_EX32(s->regs[R_FIFOTH], FIFOTH, MSIZE));
}

static void dw_mci_bmod_post_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    if (s->transfer_active && dw_mci_idmac_armed(s)) {
        dw_mci_run_idmac(s);
    }
    dw_mci_update_irq(s);
}

static void dw_mci_pldmnd_post_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    /* Any write, including zero, releases the descriptor-suspend state */
    if (s->idmac_suspended && s->transfer_active &&
        dw_mci_idmac_armed(s)) {
        s->idmac_suspended = false;
        dw_mci_set_idmac_fsm(s, DW_MCI_IDMAC_FSM_IDLE);
        dw_mci_run_idmac(s);
    }
}

static uint64_t dw_mci_pldmnd_post_read(RegisterInfo *reg, uint64_t value)
{
    return 0;
}

static void dw_mci_fifoth_post_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    s->regs[R_BMOD] = FIELD_DP32(s->regs[R_BMOD], BMOD, PBL,
                                 FIELD_EX32(value, FIFOTH, MSIZE));
    dw_mci_update_fifo_interrupts(s);
}

static void dw_mci_uhs_post_write(RegisterInfo *reg, uint64_t value)
{
    DwMciState *s = DW_MCI(reg->opaque);

    sdbus_set_voltage(&s->sdbus, value & BIT(0) ?
                      SD_VOLTAGE_1_8V : SD_VOLTAGE_3_3V);
}

static const RegisterAccessInfo dw_mci_regs_info[] = {
    { .name = "CTRL", .addr = A_CTRL, .reset = BIT(24),
      .pre_write = dw_mci_ctrl_pre_write,
      .post_write = dw_mci_ctrl_post_write },
    { .name = "PWREN", .addr = A_PWREN, .ro = UINT32_MAX ^ BIT(0),
      .post_write = dw_mci_pwren_post_write },
    { .name = "CLKDIV", .addr = A_CLKDIV, .ro = 0xffffff00 },
    { .name = "CLKSRC", .addr = A_CLKSRC, .ro = 0xfffffffc },
    { .name = "CLKENA", .addr = A_CLKENA,
      .ro = UINT32_MAX ^ (BIT(0) | BIT(16)) },
    { .name = "TMOUT", .addr = A_TMOUT, .reset = 0xffffff40 },
    { .name = "CTYPE", .addr = A_CTYPE,
      .ro = UINT32_MAX ^ (BIT(0) | BIT(16)) },
    { .name = "BLKSIZ", .addr = A_BLKSIZ, .reset = 0x200,
      .ro = 0xffff0000 },
    { .name = "BYTCNT", .addr = A_BYTCNT, .reset = 0x200 },
    { .name = "INTMASK", .addr = A_INTMASK,
      .post_write = dw_mci_ctrl_post_write },
    { .name = "CMDARG", .addr = A_CMDARG },
    { .name = "CMD", .addr = A_CMD,
      .pre_write = dw_mci_cmd_pre_write },
    { .name = "RESP0", .addr = A_RESP0, .ro = UINT32_MAX },
    { .name = "RESP1", .addr = A_RESP1, .ro = UINT32_MAX },
    { .name = "RESP2", .addr = A_RESP2, .ro = UINT32_MAX },
    { .name = "RESP3", .addr = A_RESP3, .ro = UINT32_MAX },
    { .name = "MINTSTS", .addr = A_MINTSTS, .ro = UINT32_MAX,
      .post_read = dw_mci_mintsts_post_read },
    { .name = "RINTSTS", .addr = A_RINTSTS, .w1c = UINT32_MAX,
      .post_write = dw_mci_status_post_write },
    { .name = "STATUS", .addr = A_STATUS, .ro = UINT32_MAX,
      .reset = R_STATUS_FIFO_EMPTY_MASK | R_STATUS_FIFO_TX_WMARK_MASK,
      .post_read = dw_mci_status_post_read },
    { .name = "FIFOTH", .addr = A_FIFOTH,
      .ro = 0x8000f000,
      .post_write = dw_mci_fifoth_post_write },
    { .name = "CDETECT", .addr = A_CDETECT, .ro = UINT32_MAX,
      .post_read = dw_mci_cdetect_post_read },
    { .name = "WRTPRT", .addr = A_WRTPRT, .ro = UINT32_MAX,
      .post_read = dw_mci_wrtprt_post_read },
    { .name = "GPIO", .addr = A_GPIO,
      .ro = 0xff0000ff },
    { .name = "TCBCNT", .addr = A_TCBCNT, .ro = UINT32_MAX },
    { .name = "TBBCNT", .addr = A_TBBCNT, .ro = UINT32_MAX },
    { .name = "DEBNCE", .addr = A_DEBNCE, .reset = 0x00ffffff,
      .ro = 0xff000000 },
    { .name = "USRID", .addr = A_USRID },
    { .name = "VERID", .addr = A_VERID, .ro = UINT32_MAX,
      .post_read = dw_mci_verid_post_read },
    { .name = "HCON", .addr = A_HCON, .ro = UINT32_MAX,
      .post_read = dw_mci_hcon_post_read },
    { .name = "UHS_REG", .addr = A_UHS_REG,
      .ro = UINT32_MAX ^ (BIT(0) | BIT(16)),
      .post_write = dw_mci_uhs_post_write },
    { .name = "RST_N", .addr = A_RST_N,
      .ro = UINT32_MAX ^ BIT(0) },
    { .name = "BMOD", .addr = A_BMOD, .ro = 0xffffff00,
      .pre_write = dw_mci_bmod_pre_write,
      .post_write = dw_mci_bmod_post_write },
    { .name = "PLDMND", .addr = A_PLDMND,
      .post_write = dw_mci_pldmnd_post_write,
      .post_read = dw_mci_pldmnd_post_read },
    { .name = "CDTHRCTL", .addr = A_CDTHRCTL,
      .ro = 0xf000fffe },
    { .name = "BACK_END_POWER", .addr = A_BACK_END_POWER,
      .ro = UINT32_MAX ^ BIT(0) },
    { .name = "UHS_REG_EXT", .addr = A_UHS_REG_EXT },
    { .name = "DDR_REG", .addr = A_DDR_REG },
    { .name = "ENABLE_SHIFT", .addr = A_ENABLE_SHIFT },
};

static bool dw_mci_fifo_offset(DwMciState *s, hwaddr offset)
{
    return offset >= s->data_offset && offset < DW_MCI_MMIO_SIZE;
}

static uint64_t dw_mci_read(void *opaque, hwaddr offset, unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    DwMciState *s = DW_MCI(register_array_get_owner(reg_array));
    DwMciClass *dmc = DW_MCI_GET_CLASS(s);
    uint64_t value;

    if (dmc->vendor_read && dmc->vendor_read(s, offset, &value, size)) {
        return value;
    }
    if (dw_mci_fifo_offset(s, offset)) {
        if (s->regs[R_CTRL] & R_CTRL_USE_IDMAC_MASK) {
            return 0;
        }
        return dw_mci_read_data(s, size);
    }
    if (dw_mci_idmac_offset(offset)) {
        return size == 4 ? dw_mci_read_idmac_register(s, offset) : 0;
    }
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid %u-byte access at 0x%" HWADDR_PRIx "\n",
                      TYPE_DW_MCI, size, offset);
        return 0;
    }
    return register_read_memory(opaque, offset, size);
}

static void dw_mci_write(void *opaque, hwaddr offset, uint64_t value,
                         unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    DwMciState *s = DW_MCI(register_array_get_owner(reg_array));
    DwMciClass *dmc = DW_MCI_GET_CLASS(s);

    if (dmc->vendor_write &&
        dmc->vendor_write(s, offset, value, size)) {
        return;
    }
    if (dw_mci_fifo_offset(s, offset)) {
        if (!(s->regs[R_CTRL] & R_CTRL_USE_IDMAC_MASK)) {
            dw_mci_write_data(s, value, size);
        }
        return;
    }
    if (dw_mci_idmac_offset(offset)) {
        if (size == 4) {
            dw_mci_write_idmac_register(s, offset, value);
        }
        return;
    }
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid %u-byte access at 0x%" HWADDR_PRIx "\n",
                      TYPE_DW_MCI, size, offset);
        return;
    }
    register_write_memory(opaque, offset, value, size);
}

static const MemoryRegionOps dw_mci_ops = {
    .read = dw_mci_read,
    .write = dw_mci_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
};

static void dw_mci_set_inserted(DeviceState *dev, bool inserted)
{
    DwMciState *s = DW_MCI(dev);

    if (s->card_inserted == inserted) {
        return;
    }
    s->card_inserted = inserted;
    s->regs[R_RINTSTS] |= DW_MCI_INT_CD;
    dw_mci_update_irq(s);
}

static void dw_mci_set_readonly(DeviceState *dev, bool readonly)
{
    DwMciState *s = DW_MCI(dev);

    s->card_readonly = readonly;
}

static void dw_mci_bus_class_init(ObjectClass *klass, const void *data)
{
    SDBusClass *sbc = SD_BUS_CLASS(klass);

    sbc->set_inserted = dw_mci_set_inserted;
    sbc->set_readonly = dw_mci_set_readonly;
}

static void dw_mci_reset(DeviceState *dev)
{
    DwMciState *s = DW_MCI(dev);
    DwMciClass *dmc = DW_MCI_GET_CLASS(s);
    int i;

    s->transfer_active = false;
    s->transfer_write = false;
    s->transfer_send_stop = false;
    s->transfer_remaining = 0;
    s->idmac_desc_addr = 0;
    s->idmac_suspended = false;
    s->idmac_fatal = false;
    dw_mci_fifo_reset(s);
    memset(s->regs, 0, sizeof(s->regs));

    for (i = 0; i < ARRAY_SIZE(s->regs_info); i++) {
        register_reset(&s->regs_info[i]);
    }
    s->regs[R_FIFOTH] = FIELD_DP32(0, FIFOTH, RX_WMARK,
                                   s->fifo_depth - 1);
    s->regs[R_RST_N] = 1;
    if (dmc->vendor_reset) {
        dmc->vendor_reset(s);
    }
    s->card_inserted = sdbus_get_inserted(&s->sdbus);
    s->card_readonly = sdbus_get_readonly(&s->sdbus);
    sdbus_set_voltage(&s->sdbus, SD_VOLTAGE_3_3V);
    dw_mci_update_irq(s);
}

static void dw_mci_realize(DeviceState *dev, Error **errp)
{
    DwMciState *s = DW_MCI(dev);

    if (!s->fifo_depth || s->fifo_depth > DW_MCI_FIFO_MAX_WORDS) {
        error_setg(errp, "invalid DW MCI FIFO depth %u", s->fifo_depth);
        return;
    }
    if ((s->data_offset != 0x100 && s->data_offset != 0x200) ||
        s->data_offset >= DW_MCI_MMIO_SIZE) {
        error_setg(errp, "invalid DW MCI DATA offset 0x%x",
                   s->data_offset);
    }
}

static int dw_mci_post_load(void *opaque, int version_id)
{
    DwMciState *s = opaque;
    uint32_t capacity = dw_mci_fifo_capacity(s);

    if (s->fifo_head >= capacity || s->fifo_len > capacity) {
        return -EINVAL;
    }
    dw_mci_update_fifo_interrupts(s);
    return 0;
}

static const VMStateDescription dw_mci_vmsd = {
    .name = TYPE_DW_MCI,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = dw_mci_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, DwMciState, DW_MCI_REG_COUNT),
        VMSTATE_UINT8_ARRAY(fifo, DwMciState, DW_MCI_FIFO_MAX_BYTES),
        VMSTATE_UINT32(fifo_head, DwMciState),
        VMSTATE_UINT32(fifo_len, DwMciState),
        VMSTATE_UINT32(transfer_remaining, DwMciState),
        VMSTATE_UINT64(idmac_desc_addr, DwMciState),
        VMSTATE_BOOL(transfer_active, DwMciState),
        VMSTATE_BOOL(transfer_write, DwMciState),
        VMSTATE_BOOL(transfer_send_stop, DwMciState),
        VMSTATE_BOOL(idmac_suspended, DwMciState),
        VMSTATE_BOOL(idmac_fatal, DwMciState),
        VMSTATE_BOOL(card_inserted, DwMciState),
        VMSTATE_BOOL(card_readonly, DwMciState),
        VMSTATE_END_OF_LIST()
    },
};

static void dw_mci_init(Object *obj)
{
    DwMciState *s = DW_MCI(obj);
    DwMciClass *dmc = DW_MCI_GET_CLASS(s);
    RegisterInfoArray *reg_array;

    s->verid = dmc->verid;
    s->hcon = dmc->hcon;
    s->data_offset = dmc->data_offset;
    s->fifo_depth = dmc->fifo_depth;

    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_DW_MCI_BUS,
              DEVICE(obj), "sd-bus");
    memory_region_init(&s->iomem, obj, TYPE_DW_MCI, DW_MCI_MMIO_SIZE);
    reg_array = register_init_block32(DEVICE(obj), dw_mci_regs_info,
                                      ARRAY_SIZE(dw_mci_regs_info),
                                      s->regs_info, s->regs,
                                      &dw_mci_ops, false,
                                      DW_MCI_MMIO_SIZE);
    memory_region_add_subregion(&s->iomem, 0, &reg_array->mem);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void dw_mci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    DwMciClass *dmc = DW_MCI_CLASS(klass);

    dmc->verid = 0x5342240a;
    dmc->hcon = FIELD_DP32(0, HCON, CARD_TYPE, 1);
    dmc->hcon = FIELD_DP32(dmc->hcon, HCON, HBUS_TYPE, 1);
    dmc->hcon = FIELD_DP32(dmc->hcon, HCON, HDATA_WIDTH, 1);
    dmc->hcon = FIELD_DP32(dmc->hcon, HCON, HADDR_WIDTH, 9);
    dmc->hcon = FIELD_DP32(dmc->hcon, HCON, FIFO_RAM_INSIDE, 1);
    dmc->data_offset = 0x200;
    dmc->fifo_depth = 256;

    dc->realize = dw_mci_realize;
    dc->vmsd = &dw_mci_vmsd;
    device_class_set_legacy_reset(dc, dw_mci_reset);
}

SDBus *dw_mci_get_bus(DwMciState *s)
{
    return &s->sdbus;
}

static const TypeInfo dw_mci_info = {
    .name = TYPE_DW_MCI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DwMciState),
    .instance_init = dw_mci_init,
    .class_size = sizeof(DwMciClass),
    .class_init = dw_mci_class_init,
};

static const TypeInfo dw_mci_bus_info = {
    .name = TYPE_DW_MCI_BUS,
    .parent = TYPE_SD_BUS,
    .instance_size = sizeof(SDBus),
    .class_size = sizeof(SDBusClass),
    .class_init = dw_mci_bus_class_init,
};

static void dw_mci_register_types(void)
{
    type_register_static(&dw_mci_bus_info);
    type_register_static(&dw_mci_info);
}

type_init(dw_mci_register_types)
