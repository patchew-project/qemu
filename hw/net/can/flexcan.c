/*
 * QEMU model of the NXP FLEXCAN device.
 *
 * This implementation is based on the following reference manual:
 * i.MX 6Dual/6Quad Applications Processor Reference Manual
 * Document Number: IMX6DQRM, Rev. 6, 05/2020
 *
 * Copyright (c) 2025 Matyas Bobek <matyas.bobek@gmail.com>
 *
 * Based on CTU CAN FD emulation implemented by Jan Charvat.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"
#include "hw/qdev-properties.h"
#include "trace.h"

#include "hw/net/flexcan.h"
#include "flexcan_regs.h"
#include "qemu/timer.h"

#define USE(var) (void)var;

#define DEBUG_FLEXCAN 1
#ifndef DEBUG_FLEXCAN
#define DEBUG_FLEXCAN 0
#endif

/*
 * Indicates MB w/ received frame has not been serviced yet
 * This is an emulator-only flag in position of unused (reserved) bit
 * of message buffer control register
 */
#define FLEXCAN_MB_CNT_NOT_SRV          BIT(23)
/**
 * if no MB is locked, FlexcanState.locked_mb
 * is set to FLEXCAN_NO_MB_LOCKED
 */
#define FLEXCAN_NO_MB_LOCKED            -1
/**
 * if no frame is waiting in the SMB, FlexcanState.smb_target_mbid
 * is set to FLEXCAN_SMB_EMPTY
 */
#define FLEXCAN_SMB_EMPTY               -1
/**
 * When the module is disabled or in freeze mode,
 * the timer is not running. That is indicated by setting
 * FlexcanState.timer_start to FLEXCAN_TIMER_STOPPED.
 */
#define FLEXCAN_TIMER_STOPPED           -1

/**
 * defines the end of the memory space of the implemented registers
 *
 * also prevents addressing memory after FlexcanRegs end
 */
#define FLEXCAN_ADDR_SPC_END offsetof(FlexcanRegs, _reserved6)
QEMU_BUILD_BUG_ON(FLEXCAN_ADDR_SPC_END > sizeof(FlexcanRegs));

/* These constants are returned by flexcan_fifo_rx() and flexcan_mb_rx(), */
/* Retry the other receiving mechanism (ie. message bufer or mailbox). */
#define FLEXCAN_RX_SEARCH_RETRY 0
/* The frame was received and stored. */
#define FLEXCAN_RX_SEARCH_ACCEPT 1
/* The frame was filtered out and dropped. */
#define FLEXCAN_RX_SEARCH_DROPPED 2

/*
 * These constants are returned by flexcan_mb_rx_check_mb().
 * See flexcan_mb_rx_check_mb() kerneldoc for details.
 */
#define FLEXCAN_CHECK_MB_NIL 0
#define FLEXCAN_CHECK_MB_MATCH 3
#define FLEXCAN_CHECK_MB_MATCH_NON_FREE 1
#define FLEXCAN_CHECK_MB_MATCH_LOCKED 5

static const FlexcanRegs flexcan_regs_write_mask = {
    .mcr = 0xF6EB337F,
    .ctrl = 0xFFFFFFFF,
    .timer = 0xFFFFFFFF,
    .tcr = 0xFFFFFFFF,
    .rxmgmask = 0xFFFFFFFF,
    .rx14mask = 0xFFFFFFFF,
    .rx15mask = 0xFFFFFFFF,
    .ecr = 0xFFFFFFFF,
    .esr = 0xFFFFFFFF,
    .imask2 = 0xFFFFFFFF,
    .imask1 = 0xFFFFFFFF,
    .iflag2 = 0,
    .iflag1 = 0,
    .ctrl2 = 0xFFFFFFFF,
    .esr2 = 0,
    .imeur = 0,
    .lrfr = 0,
    .crcr = 0,
    .rxfgmask = 0xFFFFFFFF,
    .rxfir = 0,
    .cbt = 0,
    ._reserved2 = 0,
    .dbg1 = 0,
    .dbg2 = 0,
    .mbs = { [0 ... 63] = {
        .can_ctrl = 0xFFFFFFFF & ~FLEXCAN_MB_CNT_NOT_SRV,
        .can_id = 0xFFFFFFFF,
        .data = { 0xFFFFFFFF, 0xFFFFFFFF },
    } },
    ._reserved4 = {0},
    .rximr = { [0 ... 63] = 0xFFFFFFFF },
    ._reserved5 = {0},
    .gfwr_mx6 = 0xFFFFFFFF,
    ._reserved6 = {0},
    ._reserved8 = {0},
    .rx_smb0_raw = {0, 0, 0, 0},
    .rx_smb1 = {0, 0, 0, 0},
};
static const FlexcanRegs flexcan_regs_reset_mask = {
    .mcr = 0x80000000,
    .ctrl = 0xFFFFFFFF,
    .timer = 0,
    .tcr = 0,
    .rxmgmask = 0xFFFFFFFF,
    .rx14mask = 0xFFFFFFFF,
    .rx15mask = 0xFFFFFFFF,
    .ecr = 0,
    .esr = 0,
    .imask2 = 0,
    .imask1 = 0,
    .iflag2 = 0,
    .iflag1 = 0,
    .ctrl2 = 0xFFFFFFFF,
    .esr2 = 0,
    .imeur = 0,
    .lrfr = 0,
    .crcr = 0,
    .rxfgmask = 0xFFFFFFFF,
    .rxfir = 0xFFFFFFFF,
    .cbt = 0,
    ._reserved2 = 0,
    .dbg1 = 0,
    .dbg2 = 0,
    .mb = {0xFFFFFFFF},
    ._reserved4 = {0},
    .rximr = {0xFFFFFFFF},
    ._reserved5 = {0},
    .gfwr_mx6 = 0,
    ._reserved6 = {0},
    ._reserved8 = {0},
    .rx_smb0_raw = {0, 0, 0, 0},
    .rx_smb1 = {0, 0, 0, 0},
};

#if DEBUG_FLEXCAN

#define DPRINTF(fmt, args...) \
    fprintf(stderr, "(%p)[%s]%s: " fmt , (void *)s, TYPE_CAN_FLEXCAN, \
            __func__, ##args);

#else /* DEBUG_FLEXCAN */

#define DPRINTF(fmt, args...) do { } while (0)

#endif /* DEBUG_FLEXCAN */

#define FLEXCAN_DBG_BUF_LEN 16

static const char *flexcan_dbg_mb_code_strs[16] = {
    "INACTIVE_RX",
    "FULL",
    "EMPTY",
    "OVERRUN",
    "INACTIVE_TX",
    "RANSWER",
    "DATA",
    "TANSWER"
};

/**
 * flexcan_dbg_mb_code() - Get the string representation of a mailbox code
 * @mb_ctrl: The mailbox control register value
 * @buf: The buffer to store the string representation
 *
 * Return: Either constant string or string formatted into @buf
 */
static const char *flexcan_dbg_mb_code(uint32_t mb_ctrl, char *buf)
{
    uint32_t code = mb_ctrl & FLEXCAN_MB_CODE_MASK;
    uint32_t code_idx = code >> 24;
    if (code == FLEXCAN_MB_CODE_TX_ABORT) {
        return "ABORT";
    }

    const char *code_str = flexcan_dbg_mb_code_strs[code_idx >> 1];
    if (code_idx & 1) {
        g_snprintf(buf, FLEXCAN_DBG_BUF_LEN, "%s+BUSY", code_str);
        return buf;
    } else {
        return code_str;
    }
}

static const char *flexcan_dbg_reg_name_fixed(hwaddr addr)
{
    if (addr >= FLEXCAN_ADDR_SPC_END) {
        return "OUT-OF-RANGE";
    }

    switch (addr) {
    case offsetof(FlexcanRegs, mcr):
        return "MCR";
    case offsetof(FlexcanRegs, ctrl):
        return "CTRL";
    case offsetof(FlexcanRegs, timer):
        return "TIMER";
    case offsetof(FlexcanRegs, esr):
        return "ESR";
    case offsetof(FlexcanRegs, rxmgmask):
        return "RXMGMASK";
    case offsetof(FlexcanRegs, rx14mask):
        return "RX14MASK";
    case offsetof(FlexcanRegs, rx15mask):
        return "RX15MASK";
    case offsetof(FlexcanRegs, rxfgmask):
        return "RXFGMASK";
    case offsetof(FlexcanRegs, ecr):
        return "ECR";
    case offsetof(FlexcanRegs, ctrl2):
        return "CTRL2";
    case offsetof(FlexcanRegs, imask2):
        return "IMASK2";
    case offsetof(FlexcanRegs, imask1):
        return "IMASK1";
    case offsetof(FlexcanRegs, iflag2):
        return "IFLAG2";
    case offsetof(FlexcanRegs, iflag1):
        return "IFLAG1";
    }
    return NULL;
}

static inline void flexcan_trace_mem_op(FlexcanState *s, hwaddr addr,
                                        uint32_t value, int size, bool is_wr)
{
    if (trace_event_get_state_backends(TRACE_FLEXCAN_MEM_OP)) {
        const char *reg_name = "unknown";
        char reg_name_buf[FLEXCAN_DBG_BUF_LEN] = { 0 };
        const char *reg_name_fixed = flexcan_dbg_reg_name_fixed(addr);
        const char *op_string = is_wr ? "write" : "read";

        if (reg_name_fixed) {
            reg_name = reg_name_fixed;
        } else if (addr >= 0x80 && addr < 0x480) {
            int mbidx = (addr - 0x80) / 16;
            g_snprintf(reg_name_buf, sizeof(reg_name_buf), "MB%i", mbidx);
            reg_name = reg_name_buf;
        } else if (addr >= 0x880 && addr < 0x9e0) {
            int id = (addr - 0x880) / 4;
            g_snprintf(reg_name_buf, sizeof(reg_name_buf), "RXIMR%i", id);
            reg_name = reg_name_buf;
        }

        trace_flexcan_mem_op(s, op_string, value, addr, reg_name, size);
    }
}

static const struct MemoryRegionOps flexcan_ops = {
    .read = flexcan_mem_read,
    .write = flexcan_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
        .accepts = flexcan_mem_accepts
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
};

static int flexcan_mb_rx(FlexcanState *s, const qemu_can_frame *frame);
static void flexcan_mb_unlock(FlexcanState *s);

/* ========== Mailbox Utils ========== */

/**
 * flexcan_mailbox_count() - Get number of enabled mailboxes
 * @s: FlexCAN device pointer
 *
 * Count is based on MCR[MAXMB] field. Note that some of those mailboxes
 * might be part of queue or queue ID filters or ordinary message buffers.
 */
static inline int flexcan_enabled_mailbox_count(const FlexcanState *s)
{
    return (s->regs.mcr & FLEXCAN_MCR_MAXMB(UINT32_MAX)) + 1;
}

/**
 * flexcan_get_first_message_buffer() - Get pointer to first message buffer
 * @s: FlexCAN device pointer
 *
 * In context of this function, message buffer means a mailbox which is not
 * a queue element nor a queue filter. Note this function does not take
 * MCR[MAXMB] into account, meaning that the returned mailbox
 * might be disabled.
 */
static FlexcanRegsMessageBuffer *flexcan_get_first_message_buffer(
                                 FlexcanState *s)
{
    if (s->regs.mcr & FLEXCAN_MCR_FEN) {
        int rffn = (s->regs.ctrl2 & FLEXCAN_CTRL2_RFFN(UINT32_MAX)) >> 24;
        return s->regs.mbs + 8 + 2 * rffn;
    } else {
        return s->regs.mbs;
    }
}

/**
 * flexcan_get_last_enabled_mailbox() - Get pointer to last enabled mailbox.
 * @s: FlexCAN device pointer
 *
 * When used with flexcan_get_first_message_buffer(), all mailboxes *ptr in
 * range `first_message_buffer() <= ptr <= last_enabled_mailbox` are valid
 * message buffer mailboxes.
 *
 * Return: Last enabled mailbox in MCR[MAXMB] sense. The mailbox might be
 * of any type.
 */
static inline FlexcanRegsMessageBuffer *flexcan_get_last_enabled_mailbox(
                                 FlexcanState *s)
{
    return s->regs.mbs + flexcan_enabled_mailbox_count(s);
}

/**
 * flexcan_get_first_filter_mailbox() - Get pointer to first queue filter.
 * @s: FlexCAN device pointer
 *
 * This function does not check if FIFO is enabled.
 *
 * Return: Pointer to first queue filter element.
 */
static inline uint32_t *flexcan_get_first_filter_mailbox(FlexcanState *s)
{
    return (uint32_t *)(s->regs.mbs + 6);
}

/**
 * flexcan_get_last_filter_mailbox() - Get pointer to last queue filter.
 * @s: FlexCAN device pointer
 *
 * This function does not check if FIFO is enabled.
 * All words in range [flexcan_get_first_filter_mailbox(),
 * flexcan_get_last_filter_mailbox()] are queue filter elements, if queue
 * is enabled.
 *
 * Return: Pointer to last queue filter element.
 */
static inline uint32_t *flexcan_get_last_filter_mailbox(FlexcanState *s)
{
    /* adding three to get pointer to the last word of the mailbox */
    uint32_t *last_enabled_elem =
        ((uint32_t *)flexcan_get_last_enabled_mailbox(s)) + 3;

    int rffn = (s->regs.ctrl2 & FLEXCAN_CTRL2_RFFN(UINT32_MAX)) >> 24;
    uint32_t *last_elem = (uint32_t *)(s->regs.mbs + 8 + 2 * rffn) - 1;

    return MIN(last_elem, last_enabled_elem);
}

/* ========== Free-running Timer ========== */
static inline int64_t flexcan_get_time(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

/**
 * flexcan_get_bitrate() - Calculate CAN bitrate (in Hz)
 * @s: FlexCAN device pointer
 *
 * The bitrate is determined by FlexCAN configuration in CTRL1 register,
 * and CCM co
 */
static uint32_t flexcan_get_bitrate(FlexcanState *s)
{
    uint32_t conf_presdiv = (s->regs.ctrl & FLEXCAN_CTRL_PRESDIV_MASK) >> 24;
    uint32_t conf_pseg1 = (s->regs.ctrl & FLEXCAN_CTRL_PSEG1_MASK) >> 19;
    uint32_t conf_pseg2 = (s->regs.ctrl & FLEXCAN_CTRL_PSEG2_MASK) >> 16;
    uint32_t conf_propseg = s->regs.ctrl & FLEXCAN_CTRL_PROPSEG_MASK;

    /* s_clock: CAN clock from CCM divivded by the prescaler */
    assert(s->ccm);
    uint32_t pe_freq = imx_ccm_get_clock_frequency(s->ccm, CLK_CAN);
    uint32_t s_freq = pe_freq / (1 + conf_presdiv);

    /* N of time quanta for segements */
    uint32_t tseg1 = 2 + conf_pseg1 + conf_propseg;
    uint32_t tseg2 = 1 + conf_pseg2;
    uint32_t total_qpb = 1 + tseg1 + tseg2;

    uint32_t bitrate = s_freq / total_qpb;

    trace_flexcan_get_bitrate(s, pe_freq, 1 + conf_presdiv, s_freq, tseg1,
                              tseg2, total_qpb, bitrate);
    return bitrate;
}

/**
 * int128_mul_6464() - Multiply two 64-bit integers into a 128-bit one
 */
static Int128 int128_muls_6464(int64_t ai, int64_t bi)
{
    uint64_t l, h;

    muls64(&l, &h, ai, bi);
    return int128_make128(l, h);
}

/**
 * flexcan_get_timestamp() - Get current value of the 16-bit free-running timer
 * @s: FlexCAN device pointer
 * @mk_unique: if true, make the timestamp unique by incrementing it if needed
 */
static uint32_t flexcan_get_timestamp(FlexcanState *s, bool mk_unique)
{
    if (s->timer_start == FLEXCAN_TIMER_STOPPED) {
        /* timer is not running, return last value */
        trace_flexcan_get_timestamp(s, -1, 0, 0, 0, s->regs.timer);
        return s->regs.timer;
    }

    int64_t current_time = flexcan_get_time();
    int64_t elapsed_time_ns = current_time - s->timer_start;
    int64_t elapsed_time_ms = elapsed_time_ns / 1000000;
    if (elapsed_time_ns < 0) {
        DPRINTF("timer overflow current_time=%li "
                "timer_start=%li elapsed_time_ns=%li\n",
                current_time, s->timer_start, elapsed_time_ns);
        return 0xFFFF;
    }

    Int128 nanoseconds_in_second = int128_makes64(1000000000);
    Int128 ncycles = int128_muls_6464(s->timer_freq, elapsed_time_ns);
    Int128 cycles128 = int128_divs(ncycles, nanoseconds_in_second);
    /* 64 bits hold for over 50k years at 10MHz */
    uint64_t cycles = int128_getlo(cycles128);

    uint32_t shift = 0;
    if (mk_unique && cycles <= s->last_rx_timer_cycles) {
        shift = 1;
        cycles = s->last_rx_timer_cycles + shift;
    }

    s->last_rx_timer_cycles = cycles;
    uint32_t rv = (uint32_t)cycles & 0xFFFF;

    trace_flexcan_get_timestamp(s, elapsed_time_ms, s->timer_freq,
                                cycles, shift, rv);
    return rv;
}

/**
 * flexcan_timer_start() - Start the free-running timer
 * @s: FlexCAN device pointer
 *
 * This should be called when the module leaves freeze mode.
 */
static void flexcan_timer_start(FlexcanState *s)
{
    if (s->timer_start != FLEXCAN_TIMER_STOPPED) {
        DPRINTF("module brought up, but timer is already running: "
                "value=%" PRIu64 "\n", s->timer_start);
    }
    s->timer_freq = flexcan_get_bitrate(s);
    s->timer_start = flexcan_get_time();
    s->last_rx_timer_cycles = 0;

    trace_flexcan_timer_start(s, s->timer_freq, s->regs.timer);
}

/**
 * flexcan_timer_stop() - Stop the free-running timer
 * @s: FlexCAN device pointer
 *
 * This should be called when the module enters freeze mode.
 * Stores the current timestamp in the TIMER register.
 */
static void flexcan_timer_stop(FlexcanState *s)
{
    s->regs.timer = flexcan_get_timestamp(s, false);
    s->timer_start = FLEXCAN_TIMER_STOPPED;

    trace_flexcan_timer_stop(s, s->timer_freq, s->regs.timer);
}

/* ========== IRQ handling ========== */
/**
 * flexcan_irq_update() - Update qemu_irq line based on interrupt registers
 * @s: FlexCAN device pointer
 */
static void flexcan_irq_update(FlexcanState *s)
{
    /* these are all interrupt sources from FlexCAN */
    /* mailbox interrupt sources */
    uint32_t mb_irqs1 = s->regs.iflag1 & s->regs.imask1;
    uint32_t mb_irqs2 = s->regs.iflag2 & s->regs.imask2;

    /**
     * these interrupts aren't currently used and they can never be raised
     *
     * bool irq_wake_up = (s->regs.mcr & FLEXCAN_MCR_WAK_MSK) &&
     *                    (s->regs.ecr & FLEXCAN_ESR_WAK_INT);
     * bool irq_bus_off = (s->regs.ctrl & FLEXCAN_CTRL_BOFF_MSK) &&
     *                    (s->regs.ecr & FLEXCAN_ESR_BOFF_INT);
     * bool irq_error = (s->regs.ctrl & FLEXCAN_CTRL_ERR_MSK) &&
     *                  (s->regs.ecr & FLEXCAN_ESR_ERR_INT);
     * bool irq_tx_warn = (s->regs.ctrl & FLEXCAN_CTRL_TWRN_MSK) &&
     *                    (s->regs.ecr & FLEXCAN_ESR_TWRN_INT);
     * bool irq_rx_warn = (s->regs.ctrl & FLEXCAN_CTRL_RWRN_MSK) &&
     *                    (s->regs.ecr & FLEXCAN_ESR_RWRN_INT);
     */

    int irq_setting = (mb_irqs1 | mb_irqs2) ? 1 : 0;
    trace_flexcan_irq_update(s, mb_irqs1, mb_irqs2, irq_setting);

    qemu_set_irq(s->irq, irq_setting);
}

/**
 * flexcan_irq_iflag_set() - Set IFLAG bit corresponding to MB mbidx
 * @s: FlexCAN device pointer
 * @mbidx: mailbox index
 */
static void flexcan_irq_iflag_set(FlexcanState *s, int mbidx)
{
    if (mbidx < 32) {
        s->regs.iflag1 |= BIT(mbidx);
    } else {
        s->regs.iflag2 |= BIT(mbidx - 32);
    }
}

/**
 * flexcan_irq_iflag_clear() - Clear IFLAG bit corresponding to MB mbidx
 * @s: FlexCAN device pointer
 * @mbidx: mailbox index
 */
static void flexcan_irq_iflag_clear(FlexcanState *s, int mbidx)
{
    if (mbidx < 32) {
        s->regs.iflag1 &= ~BIT(mbidx);
    } else {
        s->regs.iflag2 &= ~BIT(mbidx - 32);
    }
}

/* ========== RESET ========== */
static void flexcan_reset_local_state(FlexcanState *s)
{
    uint32_t *reset_mask = (uint32_t *)&flexcan_regs_reset_mask;
    for (int i = 0; i < (sizeof(FlexcanRegs) / 4); i++) {
        s->regs_raw[i] &= reset_mask[i];
    }

    s->regs.mcr |= 0x5980000F;
    s->locked_mbidx = FLEXCAN_NO_MB_LOCKED;
    s->smb_target_mbidx = FLEXCAN_SMB_EMPTY;
    s->timer_start = FLEXCAN_TIMER_STOPPED;

    trace_flexcan_reset(s);
}

static void flexcan_soft_reset(FlexcanState *s)
{
    if (s->regs.mcr & FLEXCAN_MCR_LPM_ACK) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid soft reset request in low-power mode",
                      path);
    }

    flexcan_reset_local_state(s);
}

static void flexcan_reset_enter(Object *obj, ResetType type)
{
    FlexcanState *s = CAN_FLEXCAN(obj);

    memset(&s->regs, 0, sizeof(s->regs));
    flexcan_reset_local_state(s);
}

static void flexcan_reset_hold(Object *obj, ResetType type)
{
    FlexcanState *s = CAN_FLEXCAN(obj);

    flexcan_irq_update(s);
}


/* ========== Operation mode control ========== */
/**
 * flexcan_update_esr() - Update ESR based on mode and CAN bus connection state
 * @s: FlexCAN device pointer
 */
static void flexcan_update_esr(FlexcanState *s)
{
    bool is_running = (s->regs.mcr & FLEXCAN_MCR_NOT_RDY) == 0;
    /* potentially, there could be other influences on ESR[SYNCH] */

    if (is_running && s->canbus) {
        s->regs.esr |= FLEXCAN_ESR_SYNCH | FLEXCAN_ESR_IDLE;
    } else {
        s->regs.esr &= ~(FLEXCAN_ESR_SYNCH | FLEXCAN_ESR_IDLE);
    }
}

/**
 * flexcan_update_esr() - Process MCR write
 * @s: FlexCAN device pointer
 * @pv: previously set MCR value
 *
 * This function expects the new MCR value to be already written in s->regs.mcr.
 */
static void flexcan_set_mcr(FlexcanState *s, const uint32_t pv)
{
    uint32_t cv = s->regs.mcr;

    /* -- module disable mode -- */
    if (!(pv & FLEXCAN_MCR_MDIS) && (cv & FLEXCAN_MCR_MDIS)) {
        /* transition to Module Disable mode */
        cv |= FLEXCAN_MCR_LPM_ACK;
    } else if ((pv & FLEXCAN_MCR_MDIS) && !(cv & FLEXCAN_MCR_MDIS)) {
        /* transition from Module Disable mode */
        cv &= ~FLEXCAN_MCR_LPM_ACK;
    }

    /* -- soft reset -- */
    if (!(cv & FLEXCAN_MCR_LPM_ACK) && (cv & FLEXCAN_MCR_SOFTRST)) {
        flexcan_soft_reset(s);
        cv = s->regs.mcr;
    }

    /* -- freeze mode -- */
    if (!(cv & FLEXCAN_MCR_LPM_ACK) &&
        (cv & FLEXCAN_MCR_FRZ) &&
        (cv & FLEXCAN_MCR_HALT)) {
        cv |= FLEXCAN_MCR_FRZ_ACK;
    } else {
        cv &= ~FLEXCAN_MCR_FRZ_ACK;
    }

    /* -- fifo mode -- */
    if (
        ((pv & FLEXCAN_MCR_FEN) && !(cv & FLEXCAN_MCR_FEN)) ||
        (!(pv & FLEXCAN_MCR_FEN) && (cv & FLEXCAN_MCR_FEN))
    ) {
        /* clear iflags used by fifo */
        s->regs.iflag1 &= ~(
            FLEXCAN_IFLAG_RX_FIFO_AVAILABLE |
            FLEXCAN_IFLAG_RX_FIFO_OVERFLOW |
            FLEXCAN_IFLAG_RX_FIFO_WARN
        );
    }
    if (!(pv & FLEXCAN_MCR_FEN) && (cv & FLEXCAN_MCR_FEN)) {
        /* zero out fifo region, we rely on zeroed can_ctrl for empty slots */
        memset(s->regs.mbs, 0,
               FLEXCAN_FIFO_DEPTH * sizeof(FlexcanRegsMessageBuffer));
    }

    /*
     * assert NOT_RDY bit if in disable,
     * stop (not implemented) or freeze mode
     */
    if ((cv & FLEXCAN_MCR_LPM_ACK) || (cv & FLEXCAN_MCR_FRZ_ACK)) {
        cv |= FLEXCAN_MCR_NOT_RDY;
    } else {
        cv &= ~FLEXCAN_MCR_NOT_RDY;
    }

    if ((pv & FLEXCAN_MCR_NOT_RDY) && !(cv & FLEXCAN_MCR_NOT_RDY)) {
        /* module went up, start the timer */
        flexcan_timer_start(s);
    } else if (!(pv & FLEXCAN_MCR_NOT_RDY) && (cv & FLEXCAN_MCR_NOT_RDY)) {
        /* module went down, store the current timer value */
        flexcan_timer_stop(s);
    }

    s->regs.mcr = cv;
    flexcan_update_esr(s);
    trace_flexcan_set_mcr(
        s,
        cv & FLEXCAN_MCR_LPM_ACK ? "DISABLED" : "ENABLED",
        (cv & FLEXCAN_MCR_FRZ_ACK || cv & FLEXCAN_MCR_LPM_ACK) ?
            "FROZEN" : "RUNNING",
        cv & FLEXCAN_MCR_FEN ? "FIFO" : "MAILBOX",
        cv & FLEXCAN_MCR_NOT_RDY ? "NOT_RDY" : "RDY",
        s->regs.esr & FLEXCAN_ESR_SYNCH ? "SYNC" : "NOSYNC"
    );
}

/* ========== TX ========== */
static void flexcan_transmit(FlexcanState *s, int mbidx)
{
    FlexcanRegsMessageBuffer *mb = &s->regs.mbs[mbidx];
    qemu_can_frame frame = {
        .flags = 0,
    };

    if ((s->regs.ctrl & FLEXCAN_CTRL_LOM) ||
        (s->regs.mcr & FLEXCAN_MCR_NOT_RDY)) {
        /* no transmiting in listen-only, freeze or low-power mode */
        return;
    }

    if (mb->can_ctrl & FLEXCAN_MB_CNT_IDE) {
        /* 29b ID stored in bits [0, 29) */
        uint32_t id = mb->can_id & 0x1FFFFFFF;
        frame.can_id = id | QEMU_CAN_EFF_FLAG;
    } else {
        /* 11b ID stored in bits [18, 29) */
        uint32_t id = (mb->can_id & (0x7FF << 18)) >> 18;
        frame.can_id = id;
    }

    frame.can_dlc = (mb->can_ctrl & (0xF << 16)) >> 16;

    uint32_t *frame_data = (uint32_t *)&frame.data;
    for (int i = 0; i < 2; i++) {
        stl_be_p(&frame_data[i], mb->data[i]);
    }

    if (!(s->regs.mcr & FLEXCAN_MCR_SRX_DIS)) {
        /* self-reception */
        flexcan_mb_rx(s, &frame);
    }
    if (!(s->regs.ctrl & FLEXCAN_CTRL_LPB)) {
        /* send to bus if not in loopback mode */
        if (s->canbus) {
            can_bus_client_send(&s->bus_client, &frame, 1);
        } else {
            /* todo: raise error (no ack) */
        }
    }

    uint32_t timestamp = flexcan_get_timestamp(s, true);
    mb->can_ctrl &= ~(FLEXCAN_MB_CODE_MASK | FLEXCAN_MB_CNT_TIMESTAMP_MASK);
    mb->can_ctrl |= FLEXCAN_MB_CODE_TX_INACTIVE |
                    FLEXCAN_MB_CNT_TIMESTAMP(timestamp);

    /* todo: compute the CRC */
    s->regs.crcr = FLEXCAN_CRCR_TXCRC(0) | FLEXCAN_CRCR_MBCRC(mbidx);

    flexcan_irq_iflag_set(s, mbidx);
}

static void flexcan_mb_write(FlexcanState *s, int mbid)
{
    FlexcanRegsMessageBuffer *mb = &s->regs.mbs[mbid];

    bool is_mailbox = (mb <= flexcan_get_last_enabled_mailbox(s)) &&
                     (mb >= flexcan_get_first_message_buffer(s));

    if (trace_event_get_state_backends(TRACE_FLEXCAN_MB_WRITE)) {
        char code_str_buf[FLEXCAN_DBG_BUF_LEN] = { 0 };
        const char *code_str = flexcan_dbg_mb_code(mb->can_ctrl, code_str_buf);
        trace_flexcan_mb_write(s, mbid, code_str, is_mailbox, mb->can_ctrl,
                               mb->can_id);
    }

    if (!is_mailbox) {
        /**
         * Disabled mailbox or mailbox in region of queue filters
         * was updated. Either way there is nothing to do.
         */
        return;
    }

    /* any write to message buffer clears the not_serviced flag */
    mb->can_ctrl &= ~FLEXCAN_MB_CNT_NOT_SRV;

    /**
     * todo: search for active tx mbs on transition from freeze/disable mode
     */
    switch (mb->can_ctrl & FLEXCAN_MB_CODE_MASK) {
    case FLEXCAN_MB_CODE_TX_INACTIVE:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_INACTIVE:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_EMPTY:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_FULL:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_RANSWER:
        break;

    case FLEXCAN_MB_CODE_TX_DATA:
        flexcan_transmit(s, mbid);
        break;
    case FLEXCAN_MB_CODE_TX_ABORT:
        /*
         * as transmission is instant, it can never be aborted
         * we need to set CODE in C/S back to the previous code
         */
        mb->can_ctrl &= ~FLEXCAN_MB_CODE(1);
        break;
    case FLEXCAN_MB_CODE_TX_TANSWER:
        break;
    default:
        /* prevent setting the busy bit */
        mb->can_ctrl &= ~FLEXCAN_MB_CODE_RX_BUSY_BIT;
        break;
  }

}

/* ========== RX ========== */
static void flexcan_mb_move_in(FlexcanState *s, const qemu_can_frame *frame,
                               FlexcanRegsMessageBuffer *target_mb)
{
    memset(target_mb, 0, sizeof(FlexcanRegsMessageBuffer));

    uint32_t frame_len = frame->can_dlc;
    if (frame_len > 8) {
        frame_len = 8;
    }
    uint32_t *frame_data = (uint32_t *)&frame->data;
    for (int i = 0; i < 2; i++) {
        target_mb->data[i] = ldl_be_p(&frame_data[i]);
    }

    int timestamp = flexcan_get_timestamp(s, true);
    uint32_t new_code = 0;

    switch (target_mb->can_ctrl & FLEXCAN_MB_CODE_MASK) {
    case FLEXCAN_MB_CODE_RX_FULL:
    case FLEXCAN_MB_CODE_RX_OVERRUN:
        if (target_mb->can_ctrl & FLEXCAN_MB_CNT_NOT_SRV) {
            new_code = FLEXCAN_MB_CODE_RX_OVERRUN;
        } else {
            new_code = FLEXCAN_MB_CODE_RX_FULL;
        }
        break;
    case FLEXCAN_MB_CODE_RX_RANSWER:
        assert(s->regs.ctrl2 & FLEXCAN_CTRL2_RRS);
        new_code = FLEXCAN_MB_CODE_TX_TANSWER;
        break;
    default:
        new_code = FLEXCAN_MB_CODE_RX_FULL;
    }

    target_mb->can_ctrl = new_code
        | FLEXCAN_MB_CNT_TIMESTAMP(timestamp)
        | FLEXCAN_MB_CNT_LENGTH(frame_len)
        | FLEXCAN_MB_CNT_NOT_SRV
        | FLEXCAN_MB_CNT_SRR; /* always set for received frames */
    if (frame->can_id & QEMU_CAN_RTR_FLAG) {
        target_mb->can_ctrl |= FLEXCAN_MB_CNT_RTR;
    }

    if (frame->can_id & QEMU_CAN_EFF_FLAG) {
        target_mb->can_ctrl |= FLEXCAN_MB_CNT_IDE;
        target_mb->can_id |= frame->can_id & QEMU_CAN_EFF_MASK;
    } else {
        target_mb->can_id |= (frame->can_id & QEMU_CAN_SFF_MASK) << 18;
    }
}
static void flexcan_mb_lock(FlexcanState *s, int mbidx)
{
    FlexcanRegsMessageBuffer *mb = &s->regs.mbs[mbidx];
    if ((mb > flexcan_get_last_enabled_mailbox(s)) ||
        (mb < flexcan_get_first_message_buffer(s))) {
        return;
    }
    switch (mb->can_ctrl & FLEXCAN_MB_CODE_MASK) {
    case FLEXCAN_MB_CODE_RX_FULL:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_OVERRUN:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_RANSWER:
        /* continue */
        trace_flexcan_mb_lock(s, mbidx, 1);
        break;
    default:
        trace_flexcan_mb_lock(s, mbidx, 0);
        return;
    }

    s->locked_mbidx = mbidx;
}

static void flexcan_mb_unlock(FlexcanState *s)
{
    if (s->locked_mbidx == FLEXCAN_NO_MB_LOCKED) {
        return;
    }

    int locked_mbidx = s->locked_mbidx;
    assert(locked_mbidx >= 0 && locked_mbidx < FLEXCAN_MAILBOX_COUNT);
    FlexcanRegsMessageBuffer *locked_mb = &s->regs.mbs[locked_mbidx];
    s->locked_mbidx = FLEXCAN_NO_MB_LOCKED;

    if (locked_mb >= flexcan_get_first_message_buffer(s) &&
        locked_mb <= flexcan_get_last_enabled_mailbox(s)
    ) {
        /* mark the message buffer as serviced */
        locked_mb->can_ctrl &= ~FLEXCAN_MB_CNT_NOT_SRV;
    }

    /* try move in from SMB */
    bool has_pending_frame = locked_mbidx == s->smb_target_mbidx;
    trace_flexcan_mb_unlock(s, locked_mbidx,
                            has_pending_frame ? " PENDING FRAME IN SMB" : "");

    /* todo: in low-power modes, this should be postponed until exit */
    if (has_pending_frame) {
        FlexcanRegsMessageBuffer *target_mb = &s->regs.mbs[locked_mbidx];
        memcpy(target_mb, &s->regs.rx_smb0, sizeof(FlexcanRegsMessageBuffer));

        memset(&s->regs.rx_smb0, 0, sizeof(FlexcanRegsMessageBuffer));
        s->locked_mbidx = FLEXCAN_SMB_EMPTY;

        flexcan_irq_iflag_set(s, locked_mbidx);
    }
}

bool flexcan_can_receive(CanBusClientState *client)
{
    FlexcanState *s = container_of(client, FlexcanState, bus_client);
    return !(s->regs.mcr & FLEXCAN_MCR_NOT_RDY);
}

/* --------- RX FIFO ---------- */

/**
 * flexcan_fifo_pop() - Pop message from FIFO and update IRQs
 * @s: FlexCAN device pointer
 *
 * Does not require the queue to be non-empty.
 */
static void flexcan_fifo_pop(FlexcanState *s)
{
    if (s->regs.fifo.mb_back.can_ctrl != 0) {
        /* move queue elements forward */
        memmove(&s->regs.fifo.mb_back, &s->regs.fifo.mbs_queue[0],
                sizeof(s->regs.fifo.mbs_queue));

        /* clear the first-in slot */
        memset(&s->regs.mbs[FLEXCAN_FIFO_DEPTH - 1], 0,
               sizeof(FlexcanRegsMessageBuffer));

        trace_flexcan_fifo_pop(s, 1, s->regs.fifo.mb_back.can_ctrl != 0);
    } else {
        trace_flexcan_fifo_pop(s, 0, 0);
    }

    if (s->regs.fifo.mb_back.can_ctrl != 0) {
        flexcan_irq_iflag_set(s, I_FIFO_AVAILABLE);
    } else {
        flexcan_irq_iflag_clear(s, I_FIFO_AVAILABLE);
    }
}

/**
 * flexcan_fifo_find_free_slot() - Find the first free slot in the FIFO
 * @s: FlexCAN device pointer
 *
 * Return: Pointer to the first free slot in the FIFO,
 *         or NULL if the queue is full.
 */
static FlexcanRegsMessageBuffer *flexcan_fifo_find_free_slot(FlexcanState *s)
{
    for (int i = 0; i < FLEXCAN_FIFO_DEPTH; i++) {
        FlexcanRegsMessageBuffer *mb = &s->regs.mbs[i];
        if (mb->can_ctrl == 0) {
            return mb;
        }
    }
    return NULL;
}

/**
 * flexcan_fifo_push() - Update FIFO IRQs after frame move-in
 * @s: FlexCAN device pointer
 * @slot: Target FIFO slot
 *
 * The usage is as follows:
 * 1. Get free slot pointer using flexcan_fifo_find_free_slot()
 * 2. Move the frame in if not NULL
 * 3. Call flexcan_fifo_push() regardless of the NULL pointer
 */
static void flexcan_fifo_push(FlexcanState *s, FlexcanRegsMessageBuffer *slot)
{
    if (slot) {
        int n_occupied = slot - s->regs.mbs;
        if (n_occupied == 4) { /* 4 means the 5th slot was filled in */
            /*
             * fifo occupancy increased from 4 to 5,
             * raising FIFO_WARN interrupt
             */
            flexcan_irq_iflag_set(s, I_FIFO_WARN);
        }
        flexcan_irq_iflag_set(s, I_FIFO_AVAILABLE);

        trace_flexcan_fifo_push(s, n_occupied);
    } else {
        flexcan_irq_iflag_set(s, I_FIFO_OVERFLOW);

        trace_flexcan_fifo_push(s, -1);
    }
}

static int flexcan_fifo_rx(FlexcanState *s, const qemu_can_frame *buf)
{
    /* todo: filtering. return FLEXCAN_FIFO_RX_RETRY if filtered out */
    if ((s->regs.mcr & FLEXCAN_MCR_IDAM_MASK) == FLEXCAN_MCR_IDAM_D) {
        /* all frames rejected */
        return FLEXCAN_RX_SEARCH_RETRY;
    }

    /* push message to queue if not full */
    FlexcanRegsMessageBuffer *slot = flexcan_fifo_find_free_slot(s);
    if (slot) {
        flexcan_mb_move_in(s, buf, slot);
    }
    flexcan_fifo_push(s, slot);

    return slot ? FLEXCAN_RX_SEARCH_ACCEPT : FLEXCAN_RX_SEARCH_DROPPED;
}

/* --------- RX message buffer ---------- */

/**
 * flexcan_mb_rx_check_mb() - Check if a message buffer matches a received frame
 * @s: FlexCAN device pointer
 * @buf: Frame to be received from CAN subsystem
 * @mbid: Target mailbox index. The mailbox must be a valid message buffer.
 *
 * Return: FLEXCAN_CHECK_MB_NIL if the message buffer does not match.
 *         FLEXCAN_CHECK_MB_MATCH if the message buffer matches the received
 *                                frame and is free-to-receive,
 *         FLEXCAN_CHECK_MB_MATCH_LOCKED if the message buffer matches,
 *                                       but is locked,
 *         FLEXCAN_CHECK_MB_MATCH_NON_FREE if the message buffer matches,
 *                                         but is not free-to-receive
 *                                         for some other reason.
 */
static int flexcan_mb_rx_check_mb(FlexcanState *s, const qemu_can_frame *buf,
                                  int mbid)
{
    FlexcanRegsMessageBuffer *mb = &s->regs.mbs[mbid];
    const bool is_rtr = !!(buf->can_id & QEMU_CAN_RTR_FLAG);
    const bool is_serviced = !(mb->can_ctrl & FLEXCAN_MB_CNT_NOT_SRV);
    const bool is_locked = s->locked_mbidx == mbid;

    bool is_free_to_receive = false;
    bool is_matched = false;

    switch (mb->can_ctrl & FLEXCAN_MB_CODE_MASK) {
    case FLEXCAN_MB_CODE_RX_RANSWER:
        if (is_rtr && !(s->regs.ctrl2 & FLEXCAN_CTRL2_RRS)) {
            /* todo: do the actual matching/filtering and RTR answer */
            is_matched = true;
        }
        break;
    case FLEXCAN_MB_CODE_RX_FULL:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_OVERRUN:
        is_free_to_receive = is_serviced;
        /* todo: do the actual matching/filtering */
        is_matched = true;
        break;
    case FLEXCAN_MB_CODE_RX_EMPTY:
        is_free_to_receive = true;
        /* todo: do the actual matching/filtering */
        is_matched = true;
        break;
    default:
    break;
    }

    if (trace_event_get_state_backends(TRACE_FLEXCAN_MB_RX_CHECK_MB)) {
        char code_str_buf[FLEXCAN_DBG_BUF_LEN] = { 0 };
        const char *code_str = flexcan_dbg_mb_code(mb->can_ctrl, code_str_buf);
        trace_flexcan_mb_rx_check_mb(s, mbid, code_str, is_matched,
                                     is_free_to_receive, is_serviced,
                                     is_locked);
    }

    if (is_matched && is_free_to_receive && !is_locked) {
        return FLEXCAN_CHECK_MB_MATCH;
    } else if (is_matched && !is_locked) {
        return FLEXCAN_CHECK_MB_MATCH_NON_FREE;
    } else if (is_matched) {
        return FLEXCAN_CHECK_MB_MATCH_LOCKED;
    } else {
        return FLEXCAN_CHECK_MB_NIL;
    }
}
static int flexcan_mb_rx(FlexcanState *s, const qemu_can_frame *buf)
{
    int last_not_free_to_receive_mbid = -1;
    bool last_not_free_to_receive_locked = false;

    FlexcanRegsMessageBuffer *first_mb = flexcan_get_first_message_buffer(s);
    FlexcanRegsMessageBuffer *last_mb = flexcan_get_last_enabled_mailbox(s);
    for (FlexcanRegsMessageBuffer *mb = first_mb;
         mb <= last_mb; mb++) {
        int mbid = mb - s->regs.mbs;
        int r = flexcan_mb_rx_check_mb(s, buf, mbid);
        if (r == FLEXCAN_CHECK_MB_MATCH) {
            flexcan_mb_move_in(s, buf, mb);
            flexcan_irq_iflag_set(s, mbid);
            return FLEXCAN_RX_SEARCH_ACCEPT;
        } else if (r == FLEXCAN_CHECK_MB_MATCH_NON_FREE) {
            last_not_free_to_receive_mbid = mbid;
            last_not_free_to_receive_locked = false;
        } else if (r == FLEXCAN_CHECK_MB_MATCH_LOCKED) {
            /*
             * message buffer is locked,
             * we can move in the message after it's unlocked
             */
            last_not_free_to_receive_mbid = mbid;
            last_not_free_to_receive_locked = true;
        }
    }

    if (last_not_free_to_receive_mbid >= -1) {
        if (last_not_free_to_receive_locked) {
            /*
             * copy to temporary mailbox (SMB)
             * it will be moved in when the mailbox is unlocked
             */
            s->regs.rx_smb0.can_ctrl =
                s->regs.mbs[last_not_free_to_receive_mbid].can_id;
            flexcan_mb_move_in(s, buf, &s->regs.rx_smb0);
            s->smb_target_mbidx = last_not_free_to_receive_mbid;
            return FLEXCAN_RX_SEARCH_ACCEPT;
        } else if (s->regs.mcr & FLEXCAN_MCR_IRMQ) {
            flexcan_mb_move_in(s, buf,
                               &s->regs.mbs[last_not_free_to_receive_mbid]);
            flexcan_irq_iflag_set(s, last_not_free_to_receive_mbid);
            return FLEXCAN_RX_SEARCH_ACCEPT;
        }
    }

    return FLEXCAN_RX_SEARCH_RETRY;
}

ssize_t flexcan_receive(CanBusClientState *client, const qemu_can_frame *frames,
                        size_t frames_cnt)
{
    FlexcanState *s = container_of(client, FlexcanState, bus_client);
    trace_flexcan_receive(s, frames_cnt);

    if (frames_cnt <= 0) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Error in the data received.\n",
                      path);
        return 0;
    }

    /* clear the SMB, as it would be overriden in hardware */
    memset(&s->regs.rx_smb0, 0, sizeof(FlexcanRegsMessageBuffer));
    s->smb_target_mbidx = FLEXCAN_SMB_EMPTY;

    for (size_t i = 0; i < frames_cnt; i++) {
        int r;
        const qemu_can_frame *frame = &frames[i];
        if (frame->can_id & QEMU_CAN_ERR_FLAG) {
            /* todo: error frame handling */
            continue;
        } else if (frame->flags & QEMU_CAN_FRMF_TYPE_FD) {
            /* CAN FD supported only in later FlexCAN version */
            continue;
        }

        /* todo: this order logic is not complete and needs further work */
        if (s->regs.mcr & FLEXCAN_MCR_FEN &&
            s->regs.ctrl2 & FLEXCAN_CTRL2_MRP) {
            r = flexcan_mb_rx(s, frame);
            if (r != FLEXCAN_RX_SEARCH_RETRY) {
                continue;
            }
            flexcan_fifo_rx(s, frame);
        } else if (s->regs.mcr & FLEXCAN_MCR_FEN) {
            r = flexcan_fifo_rx(s, frame);
            if (r != FLEXCAN_RX_SEARCH_RETRY) {
                continue;
            }
            flexcan_mb_rx(s, frame);
        } else {
            flexcan_mb_rx(s, frame);
        }
    }

    flexcan_irq_update(s);
    return 1;
}

/* ========== I/O handling ========== */
static void flexcan_reg_write(FlexcanState *s, hwaddr addr, uint32_t val)
{
    uint32_t write_mask = ((const uint32_t *)
        &flexcan_regs_write_mask)[addr / 4];
    uint32_t old_value = s->regs_raw[addr / 4];

    /*
     * 0 for bits that can "only be written in Freeze mode as it is blocked
     * by hardware in other modes"
     */
    const uint32_t freeze_mask_mcr = 0xDF54CC80;
    const uint32_t freeze_mask_ctrl1 = 0x0000E740;

    switch (addr) {
    case offsetof(FlexcanRegs, mcr):
        if (!(s->regs.mcr & FLEXCAN_MCR_FRZ_ACK)) {
            write_mask &= freeze_mask_mcr;
        }
        s->regs.mcr = (val & write_mask) | (old_value & ~write_mask);
        flexcan_set_mcr(s, old_value);
        break;
    case offsetof(FlexcanRegs, ctrl):
        if (!(s->regs.mcr & FLEXCAN_MCR_FRZ_ACK)) {
            write_mask &= freeze_mask_ctrl1;
        }
        s->regs.ctrl = (val & write_mask) | (old_value & ~write_mask);
        break;
    case offsetof(FlexcanRegs, iflag1):
        s->regs.iflag1 &= ~val;
        if ((s->regs.mcr & FLEXCAN_MCR_FEN) &&
            (val & FLEXCAN_IFLAG_RX_FIFO_AVAILABLE)) {
            flexcan_fifo_pop(s);
        }
        break;
    case offsetof(FlexcanRegs, iflag2):
        s->regs.iflag2 &= ~val;
        break;
    case offsetof(FlexcanRegs, ctrl2):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, ecr):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rxmgmask):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rx14mask):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rx15mask):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rxfgmask):
        /* these registers can only be written in freeze mode */
        if (!(s->regs.mcr & FLEXCAN_MCR_FRZ_ACK)) {
            break;
        }
        QEMU_FALLTHROUGH;
    default:
        /* RXIMRn can only be written in freeze mode */
        if (!(s->regs.mcr & FLEXCAN_MCR_FRZ_ACK) &&
            addr >= offsetof(FlexcanRegs, rximr) &&
            addr < offsetof(FlexcanRegs, _reserved5)) {
            break;
        }

        s->regs_raw[addr / 4] = (val & write_mask) | (old_value & ~write_mask);

        if (addr >= offsetof(FlexcanRegs, mb) &&
            addr < offsetof(FlexcanRegs, _reserved4)) {
            /* access to mailbox */
            int mbid = (addr - offsetof(FlexcanRegs, mb)) /
                            sizeof(FlexcanRegsMessageBuffer);

            if (s->locked_mbidx == mbid) {
                flexcan_mb_unlock(s);
            }

            /* check for invalid writes into FIFO region */
            if (s->regs.mcr & FLEXCAN_MCR_FEN && mbid < FLEXCAN_FIFO_DEPTH) {
                g_autofree char *path = object_get_canonical_path(OBJECT(s));
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Invalid write to Rx-FIFO structure", path);
                return;
            }

            /* run mailbox processing function on write to control word */
            if ((addr & 0xF) == 0) {
                flexcan_mb_write(s, mbid);
            }
        }
        break;
    }

    flexcan_irq_update(s);
}

void flexcan_mem_write(void *obj, hwaddr addr, uint64_t val, unsigned size)
{
    FlexcanState *s = CAN_FLEXCAN(obj);
    flexcan_trace_mem_op(s, addr, val, size, true);

    if (addr < FLEXCAN_ADDR_SPC_END) {
        flexcan_reg_write(s, addr, (uint32_t)val);
    } else {
        DPRINTF("warn: write outside of defined address space\n");
    }
}
uint64_t flexcan_mem_read(void *obj, hwaddr addr, unsigned size)
{
    FlexcanState *s = CAN_FLEXCAN(obj);

    if (addr < FLEXCAN_ADDR_SPC_END) {
        uint32_t rv = s->regs_raw[addr >> 2];

        if (addr >= offsetof(FlexcanRegs, mb) &&
            addr < offsetof(FlexcanRegs, _reserved4)) {
            /* reading from mailbox */
            hwaddr offset = addr - offsetof(FlexcanRegs, mb);
            int mbid = offset / sizeof(FlexcanRegsMessageBuffer);

            if (addr % 16 == 0 && s->locked_mbidx != mbid) {
                /* reading control word locks the mailbox */
                flexcan_mb_unlock(s);
                flexcan_mb_lock(s, mbid);
                flexcan_irq_update(s);
                rv = s->regs.mbs[mbid].can_ctrl & ~FLEXCAN_MB_CNT_NOT_SRV;
            }
        } else if (addr == offsetof(FlexcanRegs, timer)) {
            flexcan_mb_unlock(s);
            flexcan_irq_update(s);
            rv = flexcan_get_timestamp(s, false);
        }

        flexcan_trace_mem_op(s, addr, rv, size, false);
        return rv;
    } else {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid write outside valid I/O space", path);

        flexcan_trace_mem_op(s, addr, 0, size, false);
        return 0;
    }
}
bool flexcan_mem_accepts(void *obj, hwaddr addr,
                                unsigned size, bool is_write,
                                MemTxAttrs attrs)
{
    FlexcanState *s = CAN_FLEXCAN(obj);

    if ((s->regs.ctrl2 & FLEXCAN_CTRL2_WRMFRZ) &&
        (s->regs.mcr & FLEXCAN_MCR_FRZ_ACK)) {
        /* unrestricted access to FlexCAN memory in freeze mode */
        return true;
    } else if (attrs.user && (s->regs.mcr & FLEXCAN_MCR_SUPV)) {
        goto denied;
    } else if (is_write && attrs.user && addr < 4) {
        /* illegal user write to MCR */
        goto denied;
    } else if (addr >= FLEXCAN_ADDR_SPC_END) {
        /* illegal write to non-existent register */
        goto denied;
    }

    return true;
denied:
    trace_flexcan_mem_accepts(s, addr, size, is_write, !attrs.user);
    return false;
}

static CanBusClientInfo flexcan_bus_client_info = {
    .can_receive = flexcan_can_receive,
    .receive = flexcan_receive,
};

static int flexcan_connect_to_bus(FlexcanState *s, CanBusState *bus)
{
    s->bus_client.info = &flexcan_bus_client_info;

    if (can_bus_insert_client(bus, &s->bus_client) < 0) {
        return -1;
    }
    return 0;
}

void flexcan_init(Object *obj)
{
    FlexcanState *s = CAN_FLEXCAN(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    USE(s);
    USE(sbd);
}

static void flexcan_realize(DeviceState *dev, Error **errp)
{
    FlexcanState *s = CAN_FLEXCAN(dev);

    if (s->canbus) {
        if (flexcan_connect_to_bus(s, s->canbus) < 0) {
            g_autofree char *path = object_get_canonical_path(OBJECT(s));

            error_setg(errp, "%s: flexcan_connect_to_bus"
                        " failed.", path);
            return;
        }
    }

    flexcan_reset_local_state(s);

    memory_region_init_io(
        &s->iomem, OBJECT(dev), &flexcan_ops, s, TYPE_CAN_FLEXCAN, 0x4000
    );
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(SYS_BUS_DEVICE(dev)), &s->irq);
}

static const VMStateDescription vmstate_can = {
    .name = TYPE_CAN_FLEXCAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(timer_start, FlexcanState),
        VMSTATE_UINT32_ARRAY(regs_raw, FlexcanState, sizeof(FlexcanRegs) / 4),
        VMSTATE_INT32(locked_mbidx, FlexcanState),
        VMSTATE_INT32(smb_target_mbidx, FlexcanState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property flexcan_properties[] = {
    DEFINE_PROP_LINK("canbus", FlexcanState, canbus, TYPE_CAN_BUS,
                     CanBusState *),
};

static void flexcan_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = flexcan_reset_enter;
    rc->phases.hold = flexcan_reset_hold;
    dc->realize = &flexcan_realize;
    device_class_set_props(dc, flexcan_properties);
    dc->vmsd = &vmstate_can;
    dc->desc = "i.MX FLEXCAN Controller";
}

static const TypeInfo flexcan_info = {
    .name          = TYPE_CAN_FLEXCAN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(FlexcanState),
    .class_init    = flexcan_class_init,
    .instance_init = flexcan_init,
};

static void can_register_types(void)
{
    type_register_static(&flexcan_info);
}
type_init(can_register_types)
