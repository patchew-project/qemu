/*
 * QEMU NCR 53C710 SCSI I/O Processor emulation
 *
 * Copyright (c) 2026 Keith Monahan <keith@techtravels.org>
 * Copyright (c) 2006 CodeSourcery (lsi53c895a.c, written by Paul Brook)
 *
 * A rewrite of the QEMU NCR 53C710 model.  The 53C710 is an early member of
 * the NCR/LSI SCRIPTS processor family, so the SCRIPTS engine here is derived
 * from QEMU's LSI53C895A model and adapted to the 53C710's register map,
 * single byte interrupt model, big endian SCRIPTS fetch (PA-RISC) and 24 bit
 * DMA, per the NCR 53C710 Data Manual (Jun 1992) and Programmer's Guide
 * (Oct 1990).
 *
 * Derived from hw/scsi/lsi53c895a.c (Copyright (c) 2006 CodeSourcery, written
 * by Paul Brook).
 *
 * Replaces the 53C710 model written for Google Summer of Code 2025 by
 * Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com>, itself based on
 * earlier NCR 53C710 work by Helge Deller and by Toni Wilen (for UAE).
 *
 * The only instantiated device is the LASI wrapper (lasi_ncr710.c), which
 * embeds one NCR710State by value and drives it through this file.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "hw/core/irq.h"
#include "hw/scsi/ncr53c710.h"
#include "system/block-backend.h"
#include "trace.h"

/* SCNTL0 (0x00) */
#define NCR710_SCNTL0_TRG       0x01
#define NCR710_SCNTL0_AAP       0x02
#define NCR710_SCNTL0_EPG       0x04
#define NCR710_SCNTL0_EPC       0x08
#define NCR710_SCNTL0_WATN      0x10
#define NCR710_SCNTL0_START     0x20
/* ARB1|ARB0 = 0xc0 (full arbitration) at reset */

/* SCNTL1 (0x01) */
#define NCR710_SCNTL1_AESP      0x04
#define NCR710_SCNTL1_RST       0x08
#define NCR710_SCNTL1_CON       0x10
#define NCR710_SCNTL1_ESR       0x20
#define NCR710_SCNTL1_ADB       0x40
#define NCR710_SCNTL1_EXC       0x80

/* SSTAT0 (0x0d) and SIEN (0x03) share this bit layout. */
#define NCR710_STAT0_PAR        0x01
#define NCR710_STAT0_RST        0x02
#define NCR710_STAT0_UDC        0x04
#define NCR710_STAT0_SGE        0x08
#define NCR710_STAT0_SEL        0x10
#define NCR710_STAT0_STO        0x20
#define NCR710_STAT0_FCMP       0x40
#define NCR710_STAT0_MA         0x80

/* DSTAT (0x0c) and DIEN (0x39) share this bit layout. */
#define NCR710_DSTAT_IID        0x01
#define NCR710_DSTAT_WTD        0x02
#define NCR710_DSTAT_SIR        0x04
#define NCR710_DSTAT_SSI        0x08
#define NCR710_DSTAT_ABRT       0x10
#define NCR710_DSTAT_BF         0x20
#define NCR710_DSTAT_DFE        0x80   /* pure status; never raises an IRQ */

/* ISTAT (0x21) */
#define NCR710_ISTAT_DIP        0x01
#define NCR710_ISTAT_SIP        0x02
#define NCR710_ISTAT_CON        0x08
#define NCR710_ISTAT_SIGP       0x20
#define NCR710_ISTAT_RST        0x40
#define NCR710_ISTAT_ABRT       0x80

/* SBCL (0x0b): low 3 bits mirror the SCSI phase, upper bits are bus signals. */
#define NCR710_SBCL_ATN         0x08
#define NCR710_SBCL_SEL         0x10
#define NCR710_SBCL_BSY         0x20
#define NCR710_SBCL_ACK         0x40
#define NCR710_SBCL_REQ         0x80

/* SOCL (0x07) */
#define NCR710_SOCL_ATN         0x08

/* CTEST2 (0x16) */
#define NCR710_CTEST2_DACK      0x01
#define NCR710_CTEST2_SIGP      0x40

/* DMODE (0x38) */
#define NCR710_DMODE_MAN        0x01

/* DCNTL (0x3b) */
#define NCR710_DCNTL_COM        0x01
#define NCR710_DCNTL_FA         0x02
#define NCR710_DCNTL_STD        0x04
#define NCR710_DCNTL_LLM        0x08
#define NCR710_DCNTL_SSM        0x10
#define NCR710_DCNTL_EA         0x20

/* SCSI phase (MSG,C/D,I/O); matches SSTAT2[2:0]/SBCL[2:0]/DCMD[26:24]. */
#define PHASE_DO        0
#define PHASE_DI        1
#define PHASE_CMD       2
#define PHASE_ST        3
#define PHASE_MO        6
#define PHASE_MI        7
#define PHASE_MASK      7

/* Flag bit OR'd into select_tag/tag when a queue tag is valid. */
#define NCR710_TAG_VALID    (1 << 16)

/* Maximum SCRIPTS instructions to process before yielding to the CPU. */
#define NCR710_MAX_INSN     500

#define NCR710_BUF_SIZE     4096

/* waiting state machine (mirrors lsi53c895a). */
enum {
    NCR710_NOWAIT = 0,        /* SCRIPTS running or stopped */
    NCR710_WAIT_RESELECT,     /* Wait Reselect instruction issued */
    NCR710_DMA_SCRIPTS,       /* DMA invoked from within execute_script */
    NCR710_DMA_IN_PROGRESS,   /* asynchronous DMA in progress */
    NCR710_WAIT_SCRIPTS,      /* stopped on the instruction count limit */
};

enum {
    NCR710_MSG_ACTION_COMMAND = 0,
    NCR710_MSG_ACTION_DISCONNECT = 1,
    NCR710_MSG_ACTION_DOUT = 2,
    NCR710_MSG_ACTION_DIN = 3,
};

struct NCR710Request {
    SCSIRequest *req;
    uint32_t tag;
    uint32_t dma_len;
    uint8_t *dma_buf;
    uint32_t pending;     /* bytes the SCSI layer has ready while queued */
    int out;              /* nonzero if the queued transfer is DATA OUT */
    bool orphan;
    QTAILQ_ENTRY(NCR710Request) next;
};

static const char *const ncr710_reg_names[64] = {
    "SCNTL0", "SCNTL1", "SDID", "SIEN", "SCID", "SXFER", "SODL", "SOCL",
    "SFBR", "SIDL", "SBDL", "SBCL", "DSTAT", "SSTAT0", "SSTAT1", "SSTAT2",
    "DSA0", "DSA1", "DSA2", "DSA3", "CTEST0", "CTEST1", "CTEST2", "CTEST3",
    "CTEST4", "CTEST5", "CTEST6", "CTEST7", "TEMP0", "TEMP1", "TEMP2", "TEMP3",
    "DFIFO", "ISTAT", "CTEST8", "LCRC", "DBC0", "DBC1", "DBC2", "DCMD",
    "DNAD0", "DNAD1", "DNAD2", "DNAD3", "DSP0", "DSP1", "DSP2", "DSP3",
    "DSPS0", "DSPS1", "DSPS2", "DSPS3", "SCRATCH0", "SCRATCH1", "SCRATCH2",
    "SCRATCH3", "DMODE", "DIEN", "DWT", "DCNTL", "ADDER0", "ADDER1", "ADDER2",
    "ADDER3",
};

static const char *ncr710_reg_name(int offset)
{
    return (offset >= 0 && offset < 64) ? ncr710_reg_names[offset] : "???";
}

/* Forward declarations. */
static uint8_t ncr710_reg_readb(NCR710State *s, int offset);
static void ncr710_reg_writeb(NCR710State *s, int offset, uint8_t val);
static void ncr710_execute_script(NCR710State *s);
static void ncr710_set_phase(NCR710State *s, int phase);

/*
 * On the 53C700 family (including the 53C710), the Select/Reselect destination
 * ID and the SDID register are a one hot BITMASK (one bit per SCSI ID), unlike
 * the 53C8xx which use a binary encoded target number.  Convert to a number.
 */
static int ncr710_id_from_bits(unsigned bits)
{
    int i;

    bits &= 0xff;
    for (i = 0; i < 8; i++) {
        if (bits & (1u << i)) {
            return i;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "ncr710: empty destination ID bitmask; defaulting to ID 0\n");
    return 0;
}

void ncr710_soft_reset(NCR710State *s)
{
    NCR710Request *p, *p_next;

    trace_ncr710_reset();

    /*
     * A guest ISTAT.RST soft reset can land with commands still in flight: the
     * connected request in s->current and disconnected tagged requests on
     * s->queue.  Cancel them so their SCSIRequests are released and no stale
     * pointer survives the reset.  Otherwise s->current dangles into the next
     * ncr710_do_command (tripping its assert(s->current == NULL)) and the
     * queued requests leak.  scsi_req_cancel drives each through
     * ncr710_request_cancelled -> ncr710_request_orphan, which clears
     * s->current and empties s->queue, mirroring the SCNTL1.RST bus-reset
     * teardown via bus_cold_reset.  At device init or machine reset both are
     * already empty, so this is a no-op (the queue is QTAILQ_INIT'd before the
     * first reset).
     */
    if (s->current && s->current->req) {
        scsi_req_cancel(s->current->req);
    }
    QTAILQ_FOREACH_SAFE(p, &s->queue, next, p_next) {
        if (p->req) {
            scsi_req_cancel(p->req);
        }
    }

    s->carry = 0;
    s->msg_action = NCR710_MSG_ACTION_COMMAND;
    s->msg_len = 0;
    s->waiting = NCR710_NOWAIT;
    s->current_lun = 0;
    s->select_tag = 0;
    s->command_complete = 0;
    s->script_running = false;

    s->scntl0 = 0xc0;       /* full arbitration */
    s->scntl1 = 0;
    s->sdid = 0;
    s->sien = 0;
    s->scid = 0;
    s->sxfer = 0;
    s->sodl = 0;
    s->socl = 0;
    s->sfbr = 0;
    s->sidl = 0;
    s->sbdl = 0;
    s->sbcl = 0;
    s->dstat = 0;           /* DFE is OR'd in on read */
    s->sstat0 = 0;
    s->sstat1 = 0;
    s->sstat2 = 0;
    s->dsa = 0;
    s->ctest0 = 0;
    s->ctest3 = 0;
    s->ctest4 = 0;
    s->ctest5 = 0;
    s->ctest7 = 0;
    s->temp = 0;
    s->dfifo = 0;
    s->istat = 0;
    s->lcrc = 0;
    s->dbc = 0;
    s->dcmd = 0;
    s->dnad = 0;
    s->dsp = 0;
    s->dsps = 0;
    s->scratch = 0;
    s->dmode = 0;
    s->dien = 0;
    s->dwt = 0;
    s->dcntl = 0;
    s->adder = 0;

    if (s->scripts_timer) {
        timer_del(s->scripts_timer);
    }
}

static inline void ncr710_mem_read(NCR710State *s, uint32_t addr,
                                   void *buf, uint32_t len)
{
    address_space_read(s->as, addr, MEMTXATTRS_UNSPECIFIED, buf, len);
}

static inline void ncr710_mem_write(NCR710State *s, uint32_t addr,
                                    const void *buf, uint32_t len)
{
    address_space_write(s->as, addr, MEMTXATTRS_UNSPECIFIED, buf, len);
}

/*
 * SCRIPTS and the table indirect data structures are built by the big endian
 * PA-RISC CPU and fetched as longwords, so they are interpreted big endian.
 */
static inline uint32_t ncr710_read_dword(NCR710State *s, uint32_t addr)
{
    uint32_t buf;

    address_space_read(s->as, addr, MEMTXATTRS_UNSPECIFIED, &buf, 4);
    return be32_to_cpu(buf);
}

static void ncr710_stop_script(NCR710State *s)
{
    s->script_running = false;
}

static void ncr710_update_irq(NCR710State *s)
{
    int level = 0;

    if (s->dstat) {
        if (s->dstat & s->dien) {
            level = 1;
        }
        s->istat |= NCR710_ISTAT_DIP;
    } else {
        s->istat &= ~NCR710_ISTAT_DIP;
    }

    if (s->sstat0) {
        if (s->sstat0 & s->sien) {
            level = 1;
        }
        s->istat |= NCR710_ISTAT_SIP;
    } else {
        s->istat &= ~NCR710_ISTAT_SIP;
    }

    trace_ncr710_update_irq(level, s->istat, s->sstat0, s->dstat);
    qemu_set_irq(s->irq, level);
}

/* Raise a SCSI interrupt and stop SCRIPTS on a fatal/unmasked condition. */
static void ncr710_script_scsi_interrupt(NCR710State *s, int stat0)
{
    uint8_t mask;

    trace_ncr710_script_scsi_interrupt(stat0, s->sstat0);
    s->sstat0 |= stat0;
    /*
     * FCMP and SEL are nonfatal (only stop when enabled in SIEN); STO never
     * stops here (execution continues until the next SCSI bus instruction).
     */
    mask = s->sien | (uint8_t)~(NCR710_STAT0_FCMP | NCR710_STAT0_SEL);
    mask &= ~NCR710_STAT0_STO;
    if (s->sstat0 & mask) {
        ncr710_stop_script(s);
    }
    ncr710_update_irq(s);
}

/* Raise a DMA interrupt and stop SCRIPTS (all DMA interrupts are fatal). */
static void ncr710_script_dma_interrupt(NCR710State *s, int stat)
{
    trace_ncr710_script_dma_interrupt(stat, s->dstat);
    s->dstat |= stat;
    ncr710_update_irq(s);
    ncr710_stop_script(s);
}

static void ncr710_set_phase(NCR710State *s, int phase)
{
    trace_ncr710_set_phase(phase);
    s->sbcl = (s->sbcl & ~PHASE_MASK) | phase | NCR710_SBCL_REQ;
    s->sstat2 = (s->sstat2 & ~PHASE_MASK) | phase;
}

/* 53C710 has no phase mismatch jump feature; a bad phase always interrupts. */
static int ncr710_bad_phase(NCR710State *s, int new_phase)
{
    ncr710_script_scsi_interrupt(s, NCR710_STAT0_MA);
    ncr710_stop_script(s);
    ncr710_set_phase(s, new_phase);
    return 1;
}

static void ncr710_resume_script(NCR710State *s)
{
    trace_ncr710_resume_script(s->waiting);
    if (s->waiting != NCR710_DMA_SCRIPTS) {
        s->waiting = NCR710_NOWAIT;
        ncr710_execute_script(s);
    } else {
        s->waiting = NCR710_NOWAIT;
    }
}

static void ncr710_disconnect(NCR710State *s)
{
    trace_ncr710_disconnect(s->waiting);
    s->scntl1 &= ~NCR710_SCNTL1_CON;
    s->sstat2 &= ~PHASE_MASK;
    s->sbcl = 0;
}

static void ncr710_bad_selection(NCR710State *s, uint32_t id)
{
    trace_ncr710_bad_selection(id);
    ncr710_script_scsi_interrupt(s, NCR710_STAT0_STO);
    ncr710_disconnect(s);
}

static void ncr710_do_dma(NCR710State *s, int out)
{
    uint32_t count;
    uint32_t addr;
    SCSIRequest *req;
    NCR710Request *p;

    if (!s->current || !s->current->dma_len) {
        /* Wait until data is available. */
        trace_ncr710_do_dma_unavailable();
        return;
    }

    p = s->current;
    req = scsi_req_ref(s->current->req);

    count = s->dbc;
    if (count > p->dma_len) {
        count = p->dma_len;
    }

    addr = s->dnad;
    trace_ncr710_do_dma(addr, count);
    s->dnad += count;
    s->dbc -= count;
    if (p->dma_buf == NULL) {
        p->dma_buf = scsi_req_get_buf(req);
    }
    if (out) {
        ncr710_mem_read(s, addr, p->dma_buf, count);
    } else {
        ncr710_mem_write(s, addr, p->dma_buf, count);
    }
    if (p->orphan) {
        scsi_req_unref(req);
        return;
    }
    scsi_req_unref(req);

    p->dma_len -= count;
    if (p->dma_len == 0) {
        p->dma_buf = NULL;
        scsi_req_continue(req);
    } else {
        p->dma_buf += count;
        ncr710_resume_script(s);
    }
}

static void ncr710_add_msg_byte(NCR710State *s, uint8_t data)
{
    if (s->msg_len >= NCR710_MAX_MSGIN_LEN) {
        qemu_log_mask(LOG_GUEST_ERROR, "ncr710: MSG IN overflow\n");
    } else {
        s->msg[s->msg_len++] = data;
    }
}

/*
 * Reselection-interrupt mode on the 53C700 family is enabled by the SCSI
 * interrupt enable for (re)selection (SIEN.SEL).  Linux's 53c700 driver sets it
 * and expects a (re)selection interrupt when a disconnected command reselects;
 * HP-UX leaves it clear and instead parks the SCRIPTS engine on a Wait Reselect
 * instruction (handled in ncr710_wait_reselect()).  Unlike the 53C8xx, the 710
 * gates reselection response on SIEN.SEL alone, with no separate SCID.RRE bit.
 */
static inline int ncr710_irq_on_rsl(NCR710State *s)
{
    return (s->sien & NCR710_STAT0_SEL) != 0;
}

/* First disconnected request whose data the SCSI layer has made ready. */
static NCR710Request *ncr710_get_pending_req(NCR710State *s)
{
    NCR710Request *p;

    QTAILQ_FOREACH(p, &s->queue, next) {
        if (p->pending) {
            return p;
        }
    }
    return NULL;
}

/* Reselect a disconnected command to resume (continue) its data transfer. */
static void ncr710_reselect(NCR710State *s, NCR710Request *p)
{
    int id;

    assert(s->current == NULL);
    QTAILQ_REMOVE(&s->queue, p, next);
    s->current = p;

    id = (p->tag >> 8) & 0xf;
    s->sdid = 1 << id;                   /* one-hot target id (53C700 family) */
    /* 53C700 compatibility: SFBR holds the reselecting id bitmask. */
    if (!(s->dcntl & NCR710_DCNTL_COM)) {
        s->sfbr = 1 << (id & 0x7);
    }
    trace_ncr710_reselect(id);
    s->scntl1 |= NCR710_SCNTL1_CON;
    ncr710_set_phase(s, PHASE_MI);
    s->msg_action = p->out ? NCR710_MSG_ACTION_DOUT : NCR710_MSG_ACTION_DIN;
    s->current->dma_len = p->pending;
    ncr710_add_msg_byte(s, 0x80);        /* IDENTIFY (reselection) */
    if (s->current->tag & NCR710_TAG_VALID) {
        ncr710_add_msg_byte(s, 0x20);    /* SIMPLE QUEUE TAG */
        ncr710_add_msg_byte(s, p->tag & 0xff);
    }
    if (ncr710_irq_on_rsl(s)) {
        ncr710_script_scsi_interrupt(s, NCR710_STAT0_SEL);
    }
}

/*
 * Record that the SCSI layer has data ready for a queued command.  Returns 0 if
 * the device was reselected (the engine can continue), nonzero if the transfer
 * is deferred until the engine next parks at Wait Reselect.
 */
static int ncr710_queue_req(NCR710State *s, SCSIRequest *req, uint32_t len)
{
    NCR710Request *p = req->hba_private;

    p->pending = len;
    if (s->waiting == NCR710_WAIT_RESELECT ||
        (ncr710_irq_on_rsl(s) && !(s->scntl1 & NCR710_SCNTL1_CON) &&
         !(s->istat & (NCR710_ISTAT_SIP | NCR710_ISTAT_DIP)))) {
        ncr710_reselect(s, p);
        return 0;
    }
    trace_ncr710_queue_req(p->tag);
    return 1;
}

/* SCRIPTS Wait Reselect: reconnect a ready queued command, else park. */
static void ncr710_wait_reselect(NCR710State *s)
{
    NCR710Request *p;

    if (s->current) {
        return;
    }
    p = ncr710_get_pending_req(s);
    if (p) {
        ncr710_reselect(s, p);
    }
    if (s->current == NULL) {
        s->waiting = NCR710_WAIT_RESELECT;
    }
}

static void ncr710_request_orphan(NCR710State *s, NCR710Request *p)
{
    p->orphan = true;
    if (p == s->current) {
        s->current = NULL;
    } else {
        QTAILQ_REMOVE(&s->queue, p, next);
    }
    scsi_req_unref(p->req);
}

void ncr710_request_cancelled(SCSIRequest *req)
{
    NCR710State *s = ncr710_from_scsi_bus(req->bus);
    NCR710Request *p = req->hba_private;

    ncr710_request_orphan(s, p);
}

void ncr710_command_complete(SCSIRequest *req, size_t resid)
{
    NCR710State *s = ncr710_from_scsi_bus(req->bus);
    int stop = 0;

    trace_ncr710_command_complete(req->tag, req->status);
    s->status = req->status;
    s->command_complete = 2;
    if (s->waiting && s->dbc != 0) {
        /* Raise phase mismatch for short transfers. */
        stop = ncr710_bad_phase(s, PHASE_ST);
        if (stop) {
            s->waiting = NCR710_NOWAIT;
        }
    } else {
        ncr710_set_phase(s, PHASE_ST);
    }

    if (req->hba_private == s->current) {
        ncr710_request_orphan(s, s->current);
    }
    if (!stop) {
        ncr710_resume_script(s);
    }
}

void ncr710_transfer_data(SCSIRequest *req, uint32_t len)
{
    NCR710State *s = ncr710_from_scsi_bus(req->bus);
    NCR710Request *p = req->hba_private;
    int out;

    assert(!p->orphan);

    /*
     * A disconnected (queued) command, or one whose data arrives while the
     * engine is parked at Wait Reselect, must reselect before its data can be
     * moved.  ncr710_queue_req() reselects if it can, else defers the transfer
     * until the engine next parks at Wait Reselect.
     */
    if (s->waiting == NCR710_WAIT_RESELECT || p != s->current ||
        (ncr710_irq_on_rsl(s) && !(s->scntl1 & NCR710_SCNTL1_CON))) {
        if (ncr710_queue_req(s, req, len)) {
            return;
        }
    }

    out = (s->sstat2 & PHASE_MASK) == PHASE_DO;

    trace_ncr710_transfer_data(req->tag, len);
    s->current->dma_len = len;
    s->command_complete = 1;
    if (s->waiting) {
        if (s->waiting == NCR710_WAIT_RESELECT || s->dbc == 0) {
            ncr710_resume_script(s);
        } else {
            ncr710_do_dma(s, out);
        }
    }
}

static void ncr710_do_command(NCR710State *s)
{
    SCSIDevice *dev;
    uint8_t buf[16] = {0};
    uint32_t id;
    int n;

    if (s->dbc > 16) {
        s->dbc = 16;
    }
    ncr710_mem_read(s, s->dnad, buf, s->dbc);
    s->sfbr = buf[0];
    s->command_complete = 0;
    trace_ncr710_do_command(s->dbc, buf[0],
                            (buf[2] << 24) | (buf[3] << 16) |
                            (buf[4] << 8) | buf[5],
                            (buf[7] << 8) | buf[8]);

    id = (s->select_tag >> 8) & 0xf;
    dev = scsi_device_find(&s->bus, 0, id, s->current_lun);
    if (!dev) {
        ncr710_bad_selection(s, id);
        return;
    }

    assert(s->current == NULL);
    s->current = g_new0(NCR710Request, 1);
    s->current->tag = s->select_tag;
    s->current->req = scsi_req_new(dev, s->current->tag, s->current_lun, buf,
                                   s->dbc, s->current);

    n = scsi_req_enqueue(s->current->req);
    if (n) {
        if (n > 0) {
            ncr710_set_phase(s, PHASE_DI);
        } else if (n < 0) {
            ncr710_set_phase(s, PHASE_DO);
        }
        scsi_req_continue(s->current->req);
    }
    if (!s->command_complete) {
        if (n) {
            /* Stay connected; the block move waits for transfer_data. */
        } else {
            /*
             * Async no data command (e.g. SYNCHRONIZE CACHE from Linux
             * 53c700, completing via a block layer bottom half).  Park with
             * dbc = 0 so the engine stops in COMMAND instead of fabricating
             * a data phase; ncr710_command_complete() later asserts STATUS
             * and resumes on a quiescent stack.  dbc = 0 also keeps that
             * completion on its clean PHASE_ST path.
             */
            s->dbc = 0;
            s->waiting = NCR710_DMA_IN_PROGRESS;
        }
    }
}

static void ncr710_do_status(NCR710State *s)
{
    uint8_t status;

    trace_ncr710_do_status(s->dbc, s->status);
    if (s->dbc != 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ncr710: STATUS move requested %u bytes (expected 1)\n",
                      s->dbc);
    }
    s->dbc = 1;
    status = s->status;
    s->sfbr = status;
    ncr710_mem_write(s, s->dnad, &status, 1);
    ncr710_set_phase(s, PHASE_MI);
    s->msg_action = NCR710_MSG_ACTION_DISCONNECT;
    ncr710_add_msg_byte(s, 0);            /* COMMAND COMPLETE */
}

static void ncr710_do_msgin(NCR710State *s)
{
    uint8_t len;

    trace_ncr710_do_msgin(s->dbc, s->msg_len);
    if (s->msg_len == 0) {
        /*
         * MOVE WHEN MSG IN with no message queued: transfer nothing (SFBR
         * keeps its last byte).  After a declined negotiation MESSAGE REJECT
         * (msg_action COMMAND) the Linux 53c700 driver loops on MSG IN and
         * needs the target driven switch to COMMAND, so advance the phase
         * here or it spins.  (HP-UX issues its CDB directly and never
         * reenters here.)
         */
        if (s->msg_action == NCR710_MSG_ACTION_COMMAND) {
            ncr710_set_phase(s, PHASE_CMD);
        }
        return;
    }
    s->sfbr = s->msg[0];
    len = s->msg_len;
    assert(len <= NCR710_MAX_MSGIN_LEN);
    if (len > s->dbc) {
        len = s->dbc;
    }

    if (len) {
        ncr710_mem_write(s, s->dnad, s->msg, len);
        /* Drivers rely on the last byte being in SIDL. */
        s->sidl = s->msg[len - 1];
        s->msg_len -= len;
        if (s->msg_len) {
            memmove(s->msg, s->msg + len, s->msg_len);
        }
    }

    if (!s->msg_len) {
        switch (s->msg_action) {
        case NCR710_MSG_ACTION_COMMAND:
            /*
             * Stay in MSG IN: SSTAT2 latches the last REQ phase, so the driver
             * samples MSG IN at the script interrupt and the lazy MSG IN ->
             * COMMAND switch happens when its next block move requests COMMAND.
             */
            break;
        case NCR710_MSG_ACTION_DISCONNECT:
            ncr710_disconnect(s);
            break;
        case NCR710_MSG_ACTION_DOUT:
            ncr710_set_phase(s, PHASE_DO);
            break;
        case NCR710_MSG_ACTION_DIN:
            ncr710_set_phase(s, PHASE_DI);
            break;
        default:
            abort();
        }
    }
}

static uint8_t ncr710_get_msgbyte(NCR710State *s)
{
    uint8_t data;

    ncr710_mem_read(s, s->dnad, &data, 1);
    s->dnad++;
    s->dbc--;
    return data;
}

static void ncr710_skip_msgbytes(NCR710State *s, unsigned int n)
{
    s->dnad += n;
    s->dbc -= n;
}

static void ncr710_do_msgout(NCR710State *s)
{
    uint8_t msg;

    trace_ncr710_do_msgout(s->dbc);
    while (s->dbc) {
        msg = ncr710_get_msgbyte(s);
        s->sfbr = msg;

        switch (msg) {
        case 0x04:
            ncr710_disconnect(s);
            break;
        case 0x08:                       /* NOP */
            ncr710_set_phase(s, PHASE_CMD);
            break;
        case 0x01:                       /* Extended message */
            ncr710_get_msgbyte(s);       /* skip the length byte */
            msg = ncr710_get_msgbyte(s);
            switch (msg) {
            case 1:                      /* SDTR */
                ncr710_skip_msgbytes(s, 2);
                goto reject;
            case 3:                      /* WDTR */
                ncr710_skip_msgbytes(s, 1);
                goto reject;
            case 4:                      /* PPR */
                ncr710_skip_msgbytes(s, 5);
                goto reject;
            default:
                goto bad;
            }
            break;
        case 0x20:                       /* SIMPLE queue tag */
            s->select_tag &= ~0xff;
            s->select_tag |= ncr710_get_msgbyte(s) | NCR710_TAG_VALID;
            break;
        case 0x21:                       /* HEAD of queue tag */
        case 0x22:                       /* ORDERED queue tag */
            s->select_tag &= ~0xff;
            s->select_tag |= ncr710_get_msgbyte(s) | NCR710_TAG_VALID;
            break;
        case 0x0d:                       /* ABORT TAG */
        case 0x06:                       /* ABORT */
        case 0x0e:                       /* CLEAR QUEUE */
        case 0x0c:                       /* BUS DEVICE RESET */
            if (s->current && s->current->req) {
                scsi_req_cancel(s->current->req);
            }
            ncr710_disconnect(s);
            break;
        default:
            if ((msg & 0x80) == 0) {
                goto bad;
            }
            s->current_lun = msg & 7;    /* IDENTIFY */
            ncr710_set_phase(s, PHASE_CMD);
            break;
        }
    }
    return;
reject:
    /*
     * Decline SDTR/WDTR/PPR negotiation with a single MESSAGE REJECT.  NetBSD
     * osiop and Linux 53c700 accept a lone reject and move on; the HP-UX 10.20
     * install template instead requires a trailing SAVE DATA POINTERS (0x02) or
     * it spins in MSG IN until the I/O times out, while NetBSD osiop rejects a
     * 0x02 there and resets the bus.  The two are told apart by ATN as the
     * reject is read (HP-UX has deasserted it, conformant initiators have not),
     * so queue only the reject here and append the 0x02 tail in ncr710_do_msgin
     * when ATN is deasserted.  Phase stays latched at MSG IN until the CDB
     * block move requests COMMAND.
     */
    ncr710_set_phase(s, PHASE_MI);
    ncr710_add_msg_byte(s, 7);           /* MESSAGE REJECT */
    s->msg_action = NCR710_MSG_ACTION_COMMAND;
    return;
bad:
    qemu_log_mask(LOG_UNIMP, "ncr710: unimplemented message 0x%02x\n", msg);
    ncr710_set_phase(s, PHASE_MI);
    ncr710_add_msg_byte(s, 7);           /* MESSAGE REJECT */
    s->msg_action = NCR710_MSG_ACTION_COMMAND;
}

static void ncr710_memcpy(NCR710State *s, uint32_t dest, uint32_t src,
                          int count)
{
    QEMU_UNINITIALIZED uint8_t buf[NCR710_BUF_SIZE];
    int n;

    while (count) {
        n = (count > NCR710_BUF_SIZE) ? NCR710_BUF_SIZE : count;
        ncr710_mem_read(s, src, buf, n);
        ncr710_mem_write(s, dest, buf, n);
        src += n;
        dest += n;
        count -= n;
    }
}

static void ncr710_scripts_timer_start(NCR710State *s)
{
    /* The wrapper allocates this timer on the nanosecond clock. */
    timer_mod(s->scripts_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 500000);
}

void ncr710_scripts_timer_callback(void *opaque)
{
    NCR710State *s = opaque;

    s->waiting = NCR710_NOWAIT;
    ncr710_execute_script(s);
}

static void ncr710_execute_script(NCR710State *s)
{
    uint32_t insn;
    uint32_t addr;
    int opcode;
    int insn_processed = 0;
    static int reentrancy_level;

    if (s->waiting == NCR710_WAIT_SCRIPTS) {
        timer_del(s->scripts_timer);
        s->waiting = NCR710_NOWAIT;
    }

    reentrancy_level++;
    s->script_running = true;
again:
    /*
     * Yield to the CPU after NCR710_MAX_INSN instructions (so guests that spin
     * waiting on a memory change make progress), and guard against a script
     * that retriggers itself (CVE-2023-0330) via the reentrancy counter.
     */
    if (++insn_processed > NCR710_MAX_INSN || reentrancy_level > 8) {
        trace_ncr710_script_yield(insn_processed);
        s->waiting = NCR710_WAIT_SCRIPTS;
        ncr710_scripts_timer_start(s);
        reentrancy_level--;
        return;
    }

    insn = ncr710_read_dword(s, s->dsp);
    if (!insn) {
        /* Skip an empty (NULL) opcode 4 bytes at a time. */
        s->dsp += 4;
        goto again;
    }
    addr = ncr710_read_dword(s, s->dsp + 4);
    trace_ncr710_execute_script(s->dsp, insn, addr);
    s->dsps = addr;
    s->dcmd = insn >> 24;
    s->dsp += 8;

    switch (insn >> 30) {
    case 0: /* Block move */
        if (s->sstat0 & NCR710_STAT0_STO) {
            ncr710_stop_script(s);
            break;
        }
        s->dbc = insn & 0xffffff;
        if (insn & (1 << 29)) {
            /* Indirect addressing. */
            addr = ncr710_read_dword(s, addr);
        } else if (insn & (1 << 28)) {
            /* Table indirect addressing (32 bit). */
            uint32_t buf[2];
            int32_t offset = sextract32(addr, 0, 24);

            ncr710_mem_read(s, s->dsa + offset, buf, 8);
            s->dbc = be32_to_cpu(buf[0]) & 0xffffff;
            addr = be32_to_cpu(buf[1]);
        }
        if ((s->sstat2 & PHASE_MASK) != ((insn >> 24) & 7)) {
            /*
             * The target controls the phase; SSTAT2 latches the last REQ
             * phase.  The one legitimate lazy transition is MSG IN -> COMMAND
             * after a declined negotiation MESSAGE REJECT (msg_action
             * COMMAND); applying it here rather than in do_msgin keeps the
             * phase the driver samples at the script interrupt in sync.  Any
             * other requested phase is a real mismatch and interrupts with
             * M/A.
             */
            if ((s->sstat2 & PHASE_MASK) == PHASE_MI && s->msg_len == 0 &&
                s->msg_action == NCR710_MSG_ACTION_COMMAND &&
                ((insn >> 24) & 7) == PHASE_CMD) {
                ncr710_set_phase(s, PHASE_CMD);
            } else {
                trace_ncr710_block_move_badphase(s->sstat2 & PHASE_MASK,
                                                 (insn >> 24) & 7);
                ncr710_script_scsi_interrupt(s, NCR710_STAT0_MA);
                break;
            }
        }
        s->dnad = addr;
        switch (s->sstat2 & PHASE_MASK) {
        case PHASE_DO:
            s->waiting = NCR710_DMA_SCRIPTS;
            ncr710_do_dma(s, 1);
            if (s->waiting) {
                s->waiting = NCR710_DMA_IN_PROGRESS;
            }
            break;
        case PHASE_DI:
            s->waiting = NCR710_DMA_SCRIPTS;
            ncr710_do_dma(s, 0);
            if (s->waiting) {
                s->waiting = NCR710_DMA_IN_PROGRESS;
            }
            break;
        case PHASE_CMD:
            ncr710_do_command(s);
            break;
        case PHASE_ST:
            ncr710_do_status(s);
            break;
        case PHASE_MO:
            ncr710_do_msgout(s);
            break;
        case PHASE_MI:
            ncr710_do_msgin(s);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "ncr710: unimplemented phase %d\n",
                          s->sstat2 & PHASE_MASK);
        }
        s->dfifo = s->dbc & 0x7f;
        break;

    case 1: /* I/O or Read/Write register */
        opcode = (insn >> 27) & 7;
        if (opcode < 5) {
            uint32_t id;
            unsigned id_bits;

            if (insn & (1 << 25)) {
                id_bits = ncr710_read_dword(s,
                                            s->dsa + sextract32(insn, 0, 24));
            } else {
                id_bits = insn;
            }
            /* Destination ID is a one hot bitmask in bits 23:16. */
            id_bits = (id_bits >> 16) & 0xff;
            id = ncr710_id_from_bits(id_bits);
            if (insn & (1 << 26)) {
                addr = s->dsp + sextract32(addr, 0, 24);
            }
            s->dnad = addr;
            switch (opcode) {
            case 0: /* Select */
                trace_ncr710_select(id, !!(insn & (1 << 24)));
                s->sdid = id_bits;
                if (s->scntl1 & NCR710_SCNTL1_CON) {
                    s->dsp = s->dnad;
                    break;
                }
                s->sstat1 |= 0x04;       /* Won arbitration */
                if (!scsi_device_find(&s->bus, 0, id, 0)) {
                    ncr710_bad_selection(s, id);
                    break;
                }
                s->select_tag = id << 8;
                s->scntl1 |= NCR710_SCNTL1_CON;
                s->sbcl |= NCR710_SBCL_BSY;
                if (insn & (1 << 24)) {
                    /*
                     * Select with ATN/: the initiator drives a message
                     * first, so the target's first phase is MESSAGE OUT
                     * (IDENTIFY).
                     */
                    s->socl |= NCR710_SOCL_ATN;
                    s->sbcl |= NCR710_SBCL_ATN;
                    ncr710_set_phase(s, PHASE_MO);
                } else {
                    /* No ATN/: target goes straight to COMMAND phase. */
                    ncr710_set_phase(s, PHASE_CMD);
                }
                s->waiting = NCR710_NOWAIT;
                break;
            case 1: /* Wait Disconnect */
                trace_ncr710_wait_disconnect();
                s->scntl1 &= ~NCR710_SCNTL1_CON;
                break;
            case 2: /* Wait Reselect */
                /*
                 * A SIGP "start next command" kick jumps to the dispatch
                 * address latched in DNAD; otherwise reconnect a ready queued
                 * command, or park.
                 */
                if (s->istat & NCR710_ISTAT_SIGP) {
                    s->dsp = s->dnad;
                } else if (!ncr710_irq_on_rsl(s)) {
                    ncr710_wait_reselect(s);
                }
                break;
            case 3: /* Set */
                if (insn & (1 << 3)) {
                    s->socl |= NCR710_SOCL_ATN;
                    s->sbcl |= NCR710_SBCL_ATN;
                    ncr710_set_phase(s, PHASE_MO);
                }
                if (insn & (1 << 6)) {
                    s->sbcl |= NCR710_SBCL_ACK;
                }
                if (insn & (1 << 10)) {
                    s->carry = 1;
                }
                break;
            case 4: /* Clear */
                if (insn & (1 << 3)) {
                    s->socl &= ~NCR710_SOCL_ATN;
                    s->sbcl &= ~NCR710_SBCL_ATN;
                }
                if (insn & (1 << 6)) {
                    s->sbcl &= ~NCR710_SBCL_ACK;
                }
                if (insn & (1 << 10)) {
                    s->carry = 0;
                }
                break;
            }
        } else {
            /*
             * Read/Write register.  The 895a ALU operator encoding is a
             * backward compatible superset of the 710's (move/OR/AND/ADD plus
             * carry enable map onto operators 0/2/4/6/7), so this decode is
             * shared with lsi53c895a.
             */
            uint8_t op0 = 0;
            uint8_t op1 = 0;
            uint8_t data8 = (insn >> 8) & 0xff;
            int reg = ((insn >> 16) & 0x7f) | (insn & 0x80);
            int operator = (insn >> 24) & 7;

            switch (opcode) {
            case 5: /* From SFBR */
                op0 = s->sfbr;
                op1 = data8;
                break;
            case 6: /* To SFBR */
                if (operator) {
                    op0 = ncr710_reg_readb(s, reg);
                }
                op1 = data8;
                break;
            case 7: /* Read modify write */
                if (operator) {
                    op0 = ncr710_reg_readb(s, reg);
                }
                if (insn & (1 << 23)) {
                    op1 = s->sfbr;
                } else {
                    op1 = data8;
                }
                break;
            }

            switch (operator) {
            case 0: /* move */
                op0 = op1;
                break;
            case 1: /* shift left */
                op1 = op0 >> 7;
                op0 = (op0 << 1) | s->carry;
                s->carry = op1;
                break;
            case 2: /* OR */
                op0 |= op1;
                break;
            case 3: /* XOR */
                op0 ^= op1;
                break;
            case 4: /* AND */
                op0 &= op1;
                break;
            case 5: /* shift right */
                op1 = op0 & 1;
                op0 = (op0 >> 1) | (s->carry << 7);
                s->carry = op1;
                break;
            case 6: /* ADD */
                op0 += op1;
                s->carry = op0 < op1;
                break;
            case 7: /* ADC */
                op0 += op1 + s->carry;
                if (s->carry) {
                    s->carry = op0 <= op1;
                } else {
                    s->carry = op0 < op1;
                }
                break;
            }

            switch (opcode) {
            case 5: /* From SFBR */
            case 7: /* Read modify write */
                ncr710_reg_writeb(s, reg, op0);
                break;
            case 6: /* To SFBR */
                s->sfbr = op0;
                break;
            }
        }
        break;

    case 2: /* Transfer Control */
        {
            int cond;
            int jmp;

            if ((insn & 0x002e0000) == 0) {
                /* NOP */
                break;
            }
            if (s->sstat0 & NCR710_STAT0_STO) {
                ncr710_stop_script(s);
                break;
            }
            cond = jmp = (insn & (1 << 19)) != 0;
            if (cond == jmp && (insn & (1 << 21))) {
                cond = s->carry != 0;
            }
            if (cond == jmp && (insn & (1 << 17))) {
                cond = (s->sstat2 & PHASE_MASK) == ((insn >> 24) & 7);
            }
            if (cond == jmp && (insn & (1 << 18))) {
                uint8_t mask = (~insn >> 8) & 0xff;
                cond = (s->sfbr & mask) == (insn & mask);
            }
            if (cond == jmp) {
                if (insn & (1 << 23)) {
                    /* Relative address. */
                    addr = s->dsp + sextract32(addr, 0, 24);
                }
                switch ((insn >> 27) & 7) {
                case 0: /* Jump */
                    trace_ncr710_tc_jump(addr);
                    s->adder = addr;
                    s->dsp = addr;
                    break;
                case 1: /* Call */
                    trace_ncr710_tc_call(addr);
                    s->temp = s->dsp;
                    s->dsp = addr;
                    break;
                case 2: /* Return */
                    trace_ncr710_tc_return(s->temp);
                    s->dsp = s->temp;
                    break;
                case 3: /* Interrupt */
                    trace_ncr710_interrupt_insn(s->dsps);
                    ncr710_script_dma_interrupt(s, NCR710_DSTAT_SIR);
                    break;
                default:
                    trace_ncr710_illegal_insn(insn);
                    ncr710_script_dma_interrupt(s, NCR710_DSTAT_IID);
                    break;
                }
            }
        }
        break;

    case 3: /* Memory Move (Load/Store is illegal on the 710) */
        if ((insn & (1 << 29)) == 0) {
            uint32_t dest;

            dest = ncr710_read_dword(s, s->dsp);
            s->dsp += 4;
            trace_ncr710_memmove(dest, addr, insn & 0xffffff);
            ncr710_memcpy(s, dest, addr, insn & 0xffffff);
        } else {
            trace_ncr710_illegal_insn(insn);
            ncr710_script_dma_interrupt(s, NCR710_DSTAT_IID);
        }
        break;
    }

    if (s->script_running && s->waiting == NCR710_NOWAIT) {
        if (s->dcntl & NCR710_DCNTL_SSM) {
            ncr710_script_dma_interrupt(s, NCR710_DSTAT_SSI);
        } else {
            goto again;
        }
    }

    reentrancy_level--;
}

#define CASE_GET_REG24(name, addr) \
    case addr: ret = s->name & 0xff; break; \
    case addr + 1: ret = (s->name >> 8) & 0xff; break; \
    case addr + 2: ret = (s->name >> 16) & 0xff; break;

#define CASE_GET_REG32(name, addr) \
    case addr: ret = s->name & 0xff; break; \
    case addr + 1: ret = (s->name >> 8) & 0xff; break; \
    case addr + 2: ret = (s->name >> 16) & 0xff; break; \
    case addr + 3: ret = (s->name >> 24) & 0xff; break;

static uint8_t ncr710_reg_readb(NCR710State *s, int offset)
{
    uint8_t ret;

    switch (offset) {
    case NCR710_SCNTL0:
        ret = s->scntl0;
        break;
    case NCR710_SCNTL1:
        ret = s->scntl1;
        break;
    case NCR710_SDID:
        ret = s->sdid;
        break;
    case NCR710_SIEN:
        ret = s->sien;
        break;
    case NCR710_SCID:
        ret = s->scid;
        break;
    case NCR710_SXFER:
        ret = s->sxfer;
        break;
    case NCR710_SODL:
        ret = s->sodl;
        break;
    case NCR710_SOCL:
        ret = s->socl;
        break;
    case NCR710_SFBR:
        ret = s->sfbr;
        break;
    case NCR710_SIDL:
        ret = s->sidl;
        break;
    case NCR710_SBDL:
        /* Some drivers peek at the data bus during MSG IN. */
        if ((s->sstat2 & PHASE_MASK) == PHASE_MI && s->msg_len > 0) {
            ret = s->msg[0];
        } else {
            ret = 0;
        }
        break;
    case NCR710_SBCL:
        ret = s->sbcl;
        break;
    case NCR710_DSTAT:
        ret = s->dstat | NCR710_DSTAT_DFE;
        s->dstat = 0;
        ncr710_update_irq(s);
        break;
    case NCR710_SSTAT0:
        ret = s->sstat0;
        s->sstat0 = 0;
        ncr710_update_irq(s);
        break;
    case NCR710_SSTAT1:
        ret = s->sstat1;
        break;
    case NCR710_SSTAT2:
        ret = s->sstat2;
        break;
    CASE_GET_REG32(dsa, NCR710_DSA)
    case NCR710_CTEST0:
        ret = s->ctest0;
        break;
    case NCR710_CTEST1:
        ret = 0xf0;              /* DMA FIFO empty */
        break;
    case NCR710_CTEST2:
        ret = NCR710_CTEST2_DACK;
        if (s->istat & NCR710_ISTAT_SIGP) {
            s->istat &= ~NCR710_ISTAT_SIGP;
            ret |= NCR710_CTEST2_SIGP;
        }
        break;
    case NCR710_CTEST3:
        ret = s->ctest3;
        break;
    case NCR710_CTEST4:
        ret = s->ctest4;
        break;
    case NCR710_CTEST5:
        ret = s->ctest5;
        break;
    case NCR710_CTEST6:
        ret = 0;
        break;
    case NCR710_CTEST7:
        ret = s->ctest7;
        break;
    CASE_GET_REG32(temp, NCR710_TEMP)
    case NCR710_DFIFO:
        ret = s->dfifo;
        break;
    case NCR710_ISTAT:
        ret = s->istat & ~NCR710_ISTAT_CON;
        if (s->scntl1 & NCR710_SCNTL1_CON) {
            ret |= NCR710_ISTAT_CON;
        }
        break;
    case NCR710_CTEST8:
        ret = NCR710_CHIP_REVISION << 4;
        break;
    case NCR710_LCRC:
        ret = s->lcrc;
        break;
    CASE_GET_REG24(dbc, NCR710_DBC)
    case NCR710_DCMD:
        ret = s->dcmd;
        break;
    CASE_GET_REG32(dnad, NCR710_DNAD)
    CASE_GET_REG32(dsp, NCR710_DSP)
    CASE_GET_REG32(dsps, NCR710_DSPS)
    CASE_GET_REG32(scratch, NCR710_SCRATCH)
    case NCR710_DMODE:
        ret = s->dmode;
        break;
    case NCR710_DIEN:
        ret = s->dien;
        break;
    case NCR710_DWT:
        ret = s->dwt;
        break;
    case NCR710_DCNTL:
        ret = s->dcntl;
        break;
    CASE_GET_REG32(adder, NCR710_ADDER)
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ncr710: invalid read from reg %s 0x%x\n",
                      ncr710_reg_name(offset), offset);
        ret = 0xff;
        break;
    }

    trace_ncr710_reg_read(ncr710_reg_name(offset), offset, ret);
    return ret;
}

#define CASE_SET_REG24(name, addr) \
    case addr: s->name &= 0xffffff00; s->name |= val; break; \
    case addr + 1: s->name &= 0xffff00ff; s->name |= val << 8; break; \
    case addr + 2: s->name &= 0xff00ffff; s->name |= val << 16; break;

#define CASE_SET_REG32(name, addr) \
    case addr: s->name &= 0xffffff00; s->name |= val; break; \
    case addr + 1: s->name &= 0xffff00ff; s->name |= val << 8; break; \
    case addr + 2: s->name &= 0xff00ffff; s->name |= val << 16; break; \
    case addr + 3: s->name &= 0x00ffffff; s->name |= val << 24; break;

static void ncr710_reg_writeb(NCR710State *s, int offset, uint8_t val)
{
    trace_ncr710_reg_write(ncr710_reg_name(offset), offset, val);

    switch (offset) {
    case NCR710_SCNTL0:
        s->scntl0 = val;
        if (val & NCR710_SCNTL0_START) {
            qemu_log_mask(LOG_UNIMP, "ncr710: START not implemented\n");
        }
        break;
    case NCR710_SCNTL1:
        s->scntl1 = val;
        if (val & NCR710_SCNTL1_RST) {
            if (!(s->sstat0 & NCR710_STAT0_RST)) {
                bus_cold_reset(BUS(&s->bus));
                s->sstat0 |= NCR710_STAT0_RST;
                ncr710_script_scsi_interrupt(s, NCR710_STAT0_RST);
            }
        } else {
            s->sstat0 &= ~NCR710_STAT0_RST;
        }
        break;
    case NCR710_SDID:
        s->sdid = val;
        break;
    case NCR710_SIEN:
        s->sien = val;
        ncr710_update_irq(s);
        break;
    case NCR710_SCID:
        s->scid = val;
        break;
    case NCR710_SXFER:
        s->sxfer = val;
        break;
    case NCR710_SODL:
        s->sodl = val;
        break;
    case NCR710_SOCL:
        s->socl = val;
        break;
    case NCR710_SFBR:
        /* CPU may not write SFBR, but SCRIPTS register moves do. */
        s->sfbr = val;
        break;
    case NCR710_SIDL:
    case NCR710_SBDL:
        /* Read only. */
        break;
    case NCR710_SBCL:
        s->sbcl = val;
        break;
    case NCR710_DSTAT:
    case NCR710_SSTAT0:
    case NCR710_SSTAT1:
    case NCR710_SSTAT2:
        /* Read only status registers. */
        break;
    CASE_SET_REG32(dsa, NCR710_DSA)
    case NCR710_CTEST0:
        s->ctest0 = val;
        break;
    case NCR710_CTEST1:
    case NCR710_CTEST2:
        /* Read only. */
        break;
    case NCR710_CTEST3:
        s->ctest3 = val;
        break;
    case NCR710_CTEST4:
        s->ctest4 = val;
        break;
    case NCR710_CTEST5:
        s->ctest5 = val;
        break;
    case NCR710_CTEST6:
        break;
    case NCR710_CTEST7:
        s->ctest7 = val;
        break;
    CASE_SET_REG32(temp, NCR710_TEMP)
    case NCR710_DFIFO:
        s->dfifo = val;
        break;
    case NCR710_ISTAT:
        s->istat = (s->istat & ~(NCR710_ISTAT_ABRT | NCR710_ISTAT_RST |
                                 NCR710_ISTAT_SIGP)) |
                   (val & (NCR710_ISTAT_ABRT | NCR710_ISTAT_RST |
                           NCR710_ISTAT_SIGP));
        if (val & NCR710_ISTAT_RST) {
            ncr710_soft_reset(s);
            return;
        }
        if (val & NCR710_ISTAT_ABRT) {
            ncr710_script_dma_interrupt(s, NCR710_DSTAT_ABRT);
        }
        break;
    case NCR710_CTEST8:
        /*
         * Revision is read only; FLF/CLF FIFO ops do nothing (the FIFO is
         * modelled as always empty).
         */
        break;
    case NCR710_LCRC:
        /* Writing clears the longitudinal parity accumulator. */
        s->lcrc = 0;
        break;
    CASE_SET_REG24(dbc, NCR710_DBC)
    case NCR710_DCMD:
        s->dcmd = val;
        break;
    CASE_SET_REG32(dnad, NCR710_DNAD)
    case NCR710_DSP:
        s->dsp = (s->dsp & 0xffffff00) | val;
        break;
    case NCR710_DSP + 1:
        s->dsp = (s->dsp & 0xffff00ff) | (val << 8);
        break;
    case NCR710_DSP + 2:
        s->dsp = (s->dsp & 0xff00ffff) | (val << 16);
        break;
    case NCR710_DSP + 3:
        s->dsp = (s->dsp & 0x00ffffff) | (val << 24);
        /* Writing the high byte starts SCRIPTS unless in manual start mode. */
        if (!(s->dmode & NCR710_DMODE_MAN) && !s->script_running) {
            ncr710_execute_script(s);
        }
        break;
    CASE_SET_REG32(dsps, NCR710_DSPS)
    CASE_SET_REG32(scratch, NCR710_SCRATCH)
    case NCR710_DMODE:
        s->dmode = val;
        break;
    case NCR710_DIEN:
        s->dien = val;
        ncr710_update_irq(s);
        break;
    case NCR710_DWT:
        s->dwt = val;
        break;
    case NCR710_DCNTL:
        s->dcntl = val & ~NCR710_DCNTL_STD;
        if ((val & NCR710_DCNTL_STD) && !s->script_running) {
            ncr710_execute_script(s);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ncr710: invalid write to reg %s 0x%x (0x%02x)\n",
                      ncr710_reg_name(offset), offset, val);
        break;
    }
}

#undef CASE_GET_REG24
#undef CASE_GET_REG32
#undef CASE_SET_REG24
#undef CASE_SET_REG32

uint64_t ncr710_reg_read(NCR710State *s, hwaddr addr, unsigned size)
{
    /* The LASI wrapper decomposes multibyte accesses into single bytes. */
    return ncr710_reg_readb(s, addr & 0xff);
}

void ncr710_reg_write(NCR710State *s, hwaddr addr, uint64_t val, unsigned size)
{
    ncr710_reg_writeb(s, addr & 0xff, val & 0xff);
}

static int ncr710_pre_save(void *opaque)
{
    NCR710State *s = opaque;

    if (s->current) {
        assert(s->current->dma_buf == NULL);
        assert(s->current->dma_len == 0);
    }
    return 0;
}

static int ncr710_post_load(void *opaque, int version_id)
{
    NCR710State *s = opaque;

    if (s->msg_len < 0 || s->msg_len > NCR710_MAX_MSGIN_LEN) {
        return -EINVAL;
    }
    if (s->msg_action < NCR710_MSG_ACTION_COMMAND ||
        s->msg_action > NCR710_MSG_ACTION_DIN) {
        return -EINVAL;
    }
    if (s->waiting < NCR710_NOWAIT || s->waiting > NCR710_WAIT_SCRIPTS) {
        return -EINVAL;
    }
    if (s->current_lun < 0 || s->current_lun > 7) {
        return -EINVAL;
    }
    if (s->waiting == NCR710_WAIT_SCRIPTS) {
        ncr710_scripts_timer_start(s);
    }
    return 0;
}

const VMStateDescription vmstate_ncr710 = {
    .name = "ncr710",
    /*
     * Version 2: the field layout was incompatibly reworked from the model this
     * rewrite replaces (which also declared version 1).  minimum_version_id is
     * raised in lockstep so a legacy stream is rejected rather than parsed
     * positionally into the new layout.
     */
    .version_id = 2,
    .minimum_version_id = 2,
    .pre_save = ncr710_pre_save,
    .post_load = ncr710_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(carry, NCR710State),
        VMSTATE_INT32(status, NCR710State),
        VMSTATE_INT32(msg_action, NCR710State),
        VMSTATE_INT32(msg_len, NCR710State),
        VMSTATE_BUFFER(msg, NCR710State),
        VMSTATE_INT32(waiting, NCR710State),
        VMSTATE_INT32(current_lun, NCR710State),
        VMSTATE_UINT32(select_tag, NCR710State),
        VMSTATE_INT32(command_complete, NCR710State),
        VMSTATE_BOOL(script_running, NCR710State),

        VMSTATE_UINT8(scntl0, NCR710State),
        VMSTATE_UINT8(scntl1, NCR710State),
        VMSTATE_UINT8(sdid, NCR710State),
        VMSTATE_UINT8(sien, NCR710State),
        VMSTATE_UINT8(scid, NCR710State),
        VMSTATE_UINT8(sxfer, NCR710State),
        VMSTATE_UINT8(sodl, NCR710State),
        VMSTATE_UINT8(socl, NCR710State),
        VMSTATE_UINT8(sfbr, NCR710State),
        VMSTATE_UINT8(sidl, NCR710State),
        VMSTATE_UINT8(sbdl, NCR710State),
        VMSTATE_UINT8(sbcl, NCR710State),
        VMSTATE_UINT8(dstat, NCR710State),
        VMSTATE_UINT8(sstat0, NCR710State),
        VMSTATE_UINT8(sstat1, NCR710State),
        VMSTATE_UINT8(sstat2, NCR710State),
        VMSTATE_UINT32(dsa, NCR710State),
        VMSTATE_UINT8(ctest0, NCR710State),
        VMSTATE_UINT8(ctest3, NCR710State),
        VMSTATE_UINT8(ctest4, NCR710State),
        VMSTATE_UINT8(ctest5, NCR710State),
        VMSTATE_UINT8(ctest7, NCR710State),
        VMSTATE_UINT32(temp, NCR710State),
        VMSTATE_UINT8(dfifo, NCR710State),
        VMSTATE_UINT8(istat, NCR710State),
        VMSTATE_UINT8(lcrc, NCR710State),
        VMSTATE_UINT32(dbc, NCR710State),
        VMSTATE_UINT8(dcmd, NCR710State),
        VMSTATE_UINT32(dnad, NCR710State),
        VMSTATE_UINT32(dsp, NCR710State),
        VMSTATE_UINT32(dsps, NCR710State),
        VMSTATE_UINT32(scratch, NCR710State),
        VMSTATE_UINT8(dmode, NCR710State),
        VMSTATE_UINT8(dien, NCR710State),
        VMSTATE_UINT8(dwt, NCR710State),
        VMSTATE_UINT8(dcntl, NCR710State),
        VMSTATE_UINT32(adder, NCR710State),
        VMSTATE_END_OF_LIST()
    }
};
