/*
 * HP Harmony audio (LASI audio function, CS4215/AD1849 codec)
 *
 * Found in the 712, 715/64-100, 725 and B132L-B180L.  Register layout per
 * the "712 I/O Subsystem ERS".  Playback and record each have a pair of
 * 4 KiB page registers (next/current).  While the codec is out of reset a
 * queued next page moves to current as soon as the current one is done,
 * leaving the "next empty" status bit (PN/RN) set; DSTATUS.IE only gates
 * that onto the interrupt line.  A direction with no queued page idles.
 * Both directions run off the codec's single sample clock.
 *
 * Copyright (c) 2026 Keith Monahan <keith@techtravels.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <math.h>
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/audio/harmony.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "trace.h"

#define HARMONY_ID          0x00
#define HARMONY_RESET       0x04
#define HARMONY_CNTL        0x08
#define HARMONY_GAINCTL     0x0c
#define HARMONY_PNXTADD     0x10
#define HARMONY_PCURADD     0x14
#define HARMONY_RNXTADD     0x18
#define HARMONY_RCURADD     0x1c
#define HARMONY_DSTATUS     0x20
#define HARMONY_OV          0x24
#define HARMONY_PIO         0x28
#define HARMONY_DIAG        0x3c

#define RESET_RST           0x00000001

#define CNTL_C              0x80000000
#define CNTL_CODEC_REV_SHIFT 20
#define CNTL_CODEC_REV_MASK 0x0ff00000
#define CNTL_FORMAT_SHIFT   6
#define CNTL_FORMAT_MASK    0x000000c0
#define CNTL_STEREO         0x00000020
#define CNTL_RATE_MASK      0x0000001f

#define FMT_S16BE           0
#define FMT_ULAW            1
#define FMT_ALAW            2
#define FMT_U8              3

#define GAINCTL_HE          0x08000000
#define GAINCTL_LE          0x04000000
#define GAINCTL_SE          0x02000000
#define GAINCTL_IS          0x01000000
#define GAINCTL_IN_R_SHIFT  16
#define GAINCTL_IN_L_SHIFT  12
#define GAINCTL_OUT_L_SHIFT 6
#define GAINCTL_OUT_R_SHIFT 0

#define DSTATUS_IE          0x80000000
#define DSTATUS_PN          0x00000200
#define DSTATUS_PC          0x00000100
#define DSTATUS_RN          0x00000002
#define DSTATUS_RC          0x00000001

#define OV_OV               0x00000001
#define PIO_PO              0x00000002
#define DIAG_CO             0x00000001

/* audio ticks with nothing to transfer before the host voices are stopped */
#define HARMONY_IDLE_TICKS  5

/* CNTL rate field to sample rate; 0 = reserved encoding */
static const int harmony_rates[32] = {
    [0x08] = 8000,  [0x09] = 16000, [0x0a] = 27429, [0x0b] = 32000,
    [0x0e] = 48000, [0x0f] = 9600,  [0x10] = 5513,  [0x11] = 11025,
    [0x12] = 18900, [0x13] = 22050, [0x14] = 37800, [0x15] = 44100,
    [0x16] = 33075, [0x17] = 6615,
};

/* Q12 input gain, 1.5 dB steps */
static int32_t harmony_in_gain[16];

static void harmony_init_tables(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        harmony_in_gain[i] = lrint(4096.0 * pow(10.0, 1.5 * i / 20.0));
    }
}

/* G.711 */
static int16_t ulaw_to_s16(uint8_t u)
{
    int t;

    u = ~u;
    t = ((u & 0x0f) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (u & 0x80) ? (0x84 - t) : (t - 0x84);
}

static int16_t alaw_to_s16(uint8_t a)
{
    int t, seg;

    a ^= 0x55;
    t = (a & 0x0f) << 4;
    seg = (a & 0x70) >> 4;
    if (seg == 0) {
        t += 8;
    } else {
        t = (t + 0x108) << (seg - 1);
    }
    return (a & 0x80) ? t : -t;
}

/* smallest segment whose end ((base << seg) - 1) is >= val */
static int g711_segment(int val, int base)
{
    int seg;

    for (seg = 0; seg < 8; seg++) {
        if (val < (base << seg)) {
            break;
        }
    }
    return seg;
}

static uint8_t s16_to_ulaw(int16_t s)
{
    int val = s >> 2, mask, seg;

    if (val < 0) {
        val = -val;
        mask = 0x7f;
    } else {
        mask = 0xff;
    }
    val = MIN(val, 8159) + 33;
    seg = g711_segment(val, 0x40);
    if (seg >= 8) {
        return 0x7f ^ mask;
    }
    return ((seg << 4) | ((val >> (seg + 1)) & 0x0f)) ^ mask;
}

static uint8_t s16_to_alaw(int16_t s)
{
    int val = s >> 3, mask, seg;
    uint8_t aval;

    if (val >= 0) {
        mask = 0xd5;
    } else {
        mask = 0x55;
        val = -val - 1;
    }
    seg = g711_segment(val, 0x20);
    if (seg >= 8) {
        return 0x7f ^ mask;
    }
    aval = seg << 4;
    aval |= (val >> (seg < 2 ? 1 : seg)) & 0x0f;
    return aval ^ mask;
}

static int harmony_format(HarmonyState *s)
{
    return (s->cntl & CNTL_FORMAT_MASK) >> CNTL_FORMAT_SHIFT;
}

static int harmony_channels(HarmonyState *s)
{
    return (s->cntl & CNTL_STEREO) ? 2 : 1;
}

/* guest bytes per frame */
static int harmony_bpf(HarmonyState *s)
{
    return (harmony_format(s) == FMT_S16BE ? 2 : 1) * harmony_channels(s);
}

static int harmony_freq(HarmonyState *s)
{
    int freq = harmony_rates[s->cntl & CNTL_RATE_MASK];

    return freq ? freq : 8000;
}

/*
 * DMA does not stop when IE is cleared: HP-UX stops a stream by leaving
 * pages queued with IE clear, and on the next open expects to find both
 * next registers drained (PN and RN set) again.
 */
static bool harmony_running(HarmonyState *s)
{
    return !(s->reset & RESET_RST);
}

static void harmony_update_irq(HarmonyState *s)
{
    int level = (s->dstatus & DSTATUS_IE) &&
                (!s->pnxt_valid || !s->rnxt_valid);

    if (level != s->irq_level) {
        s->irq_level = level;
        trace_harmony_irq(level);
        qemu_set_irq(s->irq, level);
    }
}

static void harmony_set_voices(HarmonyState *s, bool out, bool in)
{
    if (out != s->out_active) {
        s->out_active = out;
        s->starved = false;
        if (s->voice_out) {
            audio_be_set_active_out(s->audio_be, s->voice_out, out);
        }
    }
    if (in != s->in_active) {
        s->in_active = in;
        s->in_rd = s->in_wr;
        if (s->voice_in) {
            audio_be_set_active_in(s->audio_be, s->voice_in, in);
        }
    }
}

static bool harmony_busy(HarmonyState *s)
{
    return harmony_running(s) &&
           (s->pcur_valid || s->pnxt_valid || s->rcur_valid || s->rnxt_valid);
}

/*
 * Voices start as soon as there is a page to transfer.  They are stopped
 * only by harmony_out_cb after a sustained idle spell: drivers leave both
 * directions empty for a moment at every page boundary, and restarting a
 * host capture stream that often loses its input.
 */
static void harmony_update_active(HarmonyState *s)
{
    bool out;

    if (!harmony_running(s)) {
        s->rec_seen = false;
        harmony_set_voices(s, false, false);
        return;
    }
    if (s->rcur_valid || s->rnxt_valid) {
        s->rec_seen = true;
    }
    out = s->out_active || harmony_busy(s);
    harmony_set_voices(s, out, out && s->rec_seen);
}

/* Start the queued next page in any direction that has finished its last */
static void harmony_load(HarmonyState *s)
{
    if (!harmony_running(s)) {
        return;
    }
    if (!s->pcur_valid && s->pnxt_valid) {
        s->pcuradd = s->pnxtadd;
        s->poff = 0;
        s->pcur_valid = true;
        s->pnxt_valid = false;
        trace_harmony_page("play", s->pcuradd);
    }
    if (!s->rcur_valid && s->rnxt_valid) {
        s->rcuradd = s->rnxtadd;
        s->roff = 0;
        s->rcur_valid = true;
        s->rnxt_valid = false;
        trace_harmony_page("rec", s->rcuradd);
    }
    harmony_update_irq(s);
}

static int16_t harmony_get_sample(HarmonyState *s, const uint8_t *p)
{
    switch (harmony_format(s)) {
    case FMT_S16BE:
        return (int16_t)lduw_be_p(p);
    case FMT_ULAW:
        return ulaw_to_s16(*p);
    case FMT_ALAW:
        return alaw_to_s16(*p);
    default:
        return (int16_t)((*p - 0x80) << 8);
    }
}

static void harmony_put_sample(HarmonyState *s, uint8_t *p, int16_t v)
{
    switch (harmony_format(s)) {
    case FMT_S16BE:
        stw_be_p(p, v);
        break;
    case FMT_ULAW:
        *p = s16_to_ulaw(v);
        break;
    case FMT_ALAW:
        *p = s16_to_alaw(v);
        break;
    default:
        *p = (v >> 8) + 0x80;
        break;
    }
}

/* Fetch n playback frames from guest memory into out_buf as S16 stereo */
static void harmony_play_fetch(HarmonyState *s, int n)
{
    int bpf = harmony_bpf(s);
    int bps = harmony_format(s) == FMT_S16BE ? 2 : 1;
    bool stereo = harmony_channels(s) == 2;
    int32_t att_l = s->out_att[(s->gainctl >> GAINCTL_OUT_L_SHIFT) & 0x3f];
    int32_t att_r = s->out_att[(s->gainctl >> GAINCTL_OUT_R_SHIFT) & 0x3f];
    int i;

    /*
     * HE/LE/SE are not used to mute the host output: HP-UX 10.20 sets all
     * three while idle and clears all three for playback, the opposite of
     * the "enable" reading in the Linux and BSD drivers.  The host output
     * stands for whichever jack is in use; attenuation still applies.
     */
    if (!s->pcur_valid) {
        memset(s->out_buf, 0, n * 4);
        return;
    }

    dma_memory_read(&address_space_memory, s->pcuradd + s->poff,
                    s->dma_buf, n * bpf, MEMTXATTRS_UNSPECIFIED);
    for (i = 0; i < n; i++) {
        const uint8_t *p = s->dma_buf + i * bpf;
        int16_t l = harmony_get_sample(s, p);
        int16_t r = stereo ? harmony_get_sample(s, p + bps) : l;

        s->out_buf[2 * i] = MIN(MAX((l * att_l) >> 15, INT16_MIN), INT16_MAX);
        s->out_buf[2 * i + 1] = MIN(MAX((r * att_r) >> 15, INT16_MIN),
                                    INT16_MAX);
    }
}

/* Store n record frames (host input or silence) into guest memory */
static void harmony_rec_store(HarmonyState *s, int n)
{
    int bpf = harmony_bpf(s);
    int bps = harmony_format(s) == FMT_S16BE ? 2 : 1;
    bool stereo = harmony_channels(s) == 2;
    int32_t g_l = harmony_in_gain[(s->gainctl >> GAINCTL_IN_L_SHIFT) & 0xf];
    int32_t g_r = harmony_in_gain[(s->gainctl >> GAINCTL_IN_R_SHIFT) & 0xf];
    uint32_t got = MIN(s->in_wr - s->in_rd, (uint32_t)n);
    uint32_t i;

    for (i = 0; i < got; i++) {
        uint32_t j = (s->in_rd + i) & (HARMONY_IN_RING - 1);

        s->in_buf[2 * i] = s->in_ring[2 * j];
        s->in_buf[2 * i + 1] = s->in_ring[2 * j + 1];
    }
    s->in_rd += got;
    trace_harmony_rec_read(n, got, s->in_wr - s->in_rd);
    memset(s->in_buf + 2 * got, 0, (n - got) * 4);

    for (i = 0; i < n; i++) {
        int32_t l = (s->in_buf[2 * i] * g_l) >> 12;
        int32_t r = (s->in_buf[2 * i + 1] * g_r) >> 12;
        uint8_t *p = s->dma_buf + i * bpf;

        if (l != (int16_t)l || r != (int16_t)r) {
            s->ov |= OV_OV;
            l = MIN(MAX(l, INT16_MIN), INT16_MAX);
            r = MIN(MAX(r, INT16_MIN), INT16_MAX);
        }
        if (stereo) {
            harmony_put_sample(s, p, l);
            harmony_put_sample(s, p + bps, r);
        } else {
            harmony_put_sample(s, p, (l + r) / 2);
        }
    }
    dma_memory_write(&address_space_memory, s->rcuradd + s->roff,
                     s->dma_buf, n * bpf, MEMTXATTRS_UNSPECIFIED);
}

/*
 * Retire finished pages and start queued ones.  Returns true if a direction
 * ran out with nothing queued: the transfer then waits for the driver to
 * service the interrupt rather than consuming more of this audio tick.
 */
static bool harmony_page_end(HarmonyState *s)
{
    /* a format change mid-page can leave less than a frame; drop it */
    int bpf = harmony_bpf(s);
    bool p_end = s->pcur_valid && s->poff + bpf > HARMONY_PAGE_SIZE;
    bool r_end = s->rcur_valid && s->roff + bpf > HARMONY_PAGE_SIZE;

    if (p_end) {
        s->pcur_valid = false;
    }
    if (r_end) {
        s->rcur_valid = false;
    }
    harmony_load(s);
    if ((p_end && !s->pcur_valid) || (r_end && !s->rcur_valid)) {
        trace_harmony_starved(!s->pcur_valid, !s->rcur_valid);
        return true;
    }
    return false;
}

/*
 * Transfer up to the given number of frames, as far as the output voice
 * accepts them.  Stops early, marking the stream starved, when a direction
 * runs out with nothing queued.
 */
static void harmony_fill(HarmonyState *s, int frames)
{
    while (frames > 0) {
        int bpf, n;
        size_t written;

        if (harmony_page_end(s)) {
            s->starved = true;
            break;
        }
        if (!(s->pcur_valid || s->rcur_valid)) {
            break;
        }
        s->starved = false;

        bpf = harmony_bpf(s);
        n = MIN(frames, HARMONY_MIX_FRAMES);
        if (s->pcur_valid) {
            n = MIN(n, (HARMONY_PAGE_SIZE - s->poff) / bpf);
        }
        if (s->rcur_valid) {
            n = MIN(n, (HARMONY_PAGE_SIZE - s->roff) / bpf);
        }

        harmony_play_fetch(s, n);
        written = audio_be_write(s->audio_be, s->voice_out, s->out_buf, n * 4);
        n = written / 4;
        if (n == 0) {
            break;
        }
        if (s->pcur_valid) {
            s->poff += n * bpf;
        }
        if (s->rcur_valid) {
            harmony_rec_store(s, n);
            s->roff += n * bpf;
        }
        frames -= n;
    }
}

/*
 * A page queued while the stream is starved is picked up at once instead
 * of at the next audio tick.  Backends that call back rarely (PulseAudio
 * after an underrun raises its latency) would otherwise hold the stream to
 * one page per tick.  The voice only accepts what it has room for, so this
 * cannot run ahead of the host clock.  While record is in use, wait for
 * its page too so the two directions stay in step.
 */
static void harmony_resume(HarmonyState *s)
{
    if (!s->starved || !s->out_active || !s->voice_out) {
        return;
    }
    if (s->rec_seen && !s->rcur_valid) {
        return;
    }
    harmony_fill(s, INT_MAX / 4);
}

static void harmony_out_cb(void *opaque, int avail)
{
    HarmonyState *s = opaque;

    harmony_fill(s, avail / 4);

    if (harmony_busy(s)) {
        s->idle_ticks = 0;
    } else if (++s->idle_ticks >= HARMONY_IDLE_TICKS) {
        s->idle_ticks = 0;
        s->rec_seen = false;
        harmony_set_voices(s, false, false);
        return;
    }
    harmony_update_active(s);
}

/*
 * Drain the input voice into the ring.  Keep at most a quarter second of
 * backlog so a host input running faster than the codec clock doesn't
 * build up latency.
 */
static void harmony_in_cb(void *opaque, int avail)
{
    HarmonyState *s = opaque;
    uint32_t max_backlog = MIN(harmony_freq(s) / 4, HARMONY_IN_RING - 1);
    int frames = avail / 4;

    trace_harmony_in_cb(avail);
    while (frames > 0) {
        int n = MIN(frames, HARMONY_MIX_FRAMES);
        size_t got = audio_be_read(s->audio_be, s->voice_in, s->in_buf, n * 4);
        int i;

        n = got / 4;
        if (n == 0) {
            break;
        }
        for (i = 0; i < n; i++) {
            uint32_t j = (s->in_wr + i) & (HARMONY_IN_RING - 1);

            s->in_ring[2 * j] = s->in_buf[2 * i];
            s->in_ring[2 * j + 1] = s->in_buf[2 * i + 1];
        }
        s->in_wr += n;
        frames -= n;
    }
    if (s->in_wr - s->in_rd > max_backlog) {
        s->in_rd = s->in_wr - max_backlog;
    }
}

static void harmony_open_voices(HarmonyState *s)
{
    struct audsettings as = {
        .freq = harmony_freq(s),
        .nchannels = 2,
        .fmt = AUDIO_FORMAT_S16,
        .big_endian = HOST_BIG_ENDIAN,
    };

    if (s->voice_out && as.freq == s->freq) {
        return;
    }
    s->freq = as.freq;
    trace_harmony_config(as.freq, harmony_format(s), harmony_channels(s));

    s->voice_out = audio_be_open_out(s->audio_be, s->voice_out, "harmony.out",
                                     s, harmony_out_cb, &as);
    s->voice_in = audio_be_open_in(s->audio_be, s->voice_in, "harmony.in",
                                   s, harmony_in_cb, &as);

    /* reopening leaves the voices inactive; restore their state */
    if (s->voice_out) {
        audio_be_set_active_out(s->audio_be, s->voice_out, s->out_active);
    }
    if (s->voice_in) {
        audio_be_set_active_in(s->audio_be, s->voice_in, s->in_active);
    }
    harmony_update_active(s);
}

static const char *harmony_reg_name(hwaddr addr)
{
    switch (addr) {
    case HARMONY_ID:        return "ID";
    case HARMONY_RESET:     return "RESET";
    case HARMONY_CNTL:      return "CNTL";
    case HARMONY_GAINCTL:   return "GAINCTL";
    case HARMONY_PNXTADD:   return "PNXTADD";
    case HARMONY_PCURADD:   return "PCURADD";
    case HARMONY_RNXTADD:   return "RNXTADD";
    case HARMONY_RCURADD:   return "RCURADD";
    case HARMONY_DSTATUS:   return "DSTATUS";
    case HARMONY_OV:        return "OV";
    case HARMONY_PIO:       return "PIO";
    case HARMONY_DIAG:      return "DIAG";
    default:                return "?";
    }
}

static uint64_t harmony_read(void *opaque, hwaddr addr, unsigned size)
{
    HarmonyState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case HARMONY_ID:
        val = s->id;
        break;
    case HARMONY_RESET:
        val = s->reset;
        break;
    case HARMONY_CNTL:
        /* control writes complete at once, so C never reads back set */
        val = (s->cntl & ~(CNTL_C | CNTL_CODEC_REV_MASK)) |
              (s->codec_rev << CNTL_CODEC_REV_SHIFT);
        break;
    case HARMONY_GAINCTL:
        val = s->gainctl;
        break;
    case HARMONY_PNXTADD:
        val = s->pnxtadd;
        break;
    case HARMONY_PCURADD:
        val = s->pcuradd + s->poff;
        break;
    case HARMONY_RNXTADD:
        val = s->rnxtadd;
        break;
    case HARMONY_RCURADD:
        val = s->rcuradd + s->roff;
        break;
    case HARMONY_DSTATUS:
        val = s->dstatus & DSTATUS_IE;
        val |= s->pnxt_valid ? 0 : DSTATUS_PN;
        val |= s->rnxt_valid ? 0 : DSTATUS_RN;
        if (harmony_running(s)) {
            val |= s->pcur_valid ? DSTATUS_PC : 0;
            val |= s->rcur_valid ? DSTATUS_RC : 0;
        }
        break;
    case HARMONY_OV:
        val = s->ov;
        break;
    case HARMONY_PIO:
        val = s->pio & PIO_PO;
        break;
    case HARMONY_DIAG:
        val = (s->reset & RESET_RST) ? 0 : DIAG_CO;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "harmony: read from unknown register "
                      "0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    trace_harmony_reg_read(addr, harmony_reg_name(addr), val);
    return val;
}

static void harmony_write(void *opaque, hwaddr addr, uint64_t val64,
                          unsigned size)
{
    HarmonyState *s = opaque;
    uint32_t val = val64;

    trace_harmony_reg_write(addr, harmony_reg_name(addr), val);

    switch (addr) {
    case HARMONY_RESET:
        s->reset = val & RESET_RST;
        harmony_load(s);
        harmony_update_active(s);
        break;
    case HARMONY_CNTL:
        s->cntl = val & ~CNTL_C;
        harmony_open_voices(s);
        break;
    case HARMONY_GAINCTL:
        s->gainctl = val;
        break;
    case HARMONY_PNXTADD:
        s->pnxtadd = val;
        s->pnxt_valid = true;
        harmony_load(s);
        harmony_update_active(s);
        harmony_resume(s);
        break;
    case HARMONY_RNXTADD:
        s->rnxtadd = val;
        s->rnxt_valid = true;
        harmony_load(s);
        harmony_update_active(s);
        harmony_resume(s);
        break;
    case HARMONY_DSTATUS:
        s->dstatus = val & DSTATUS_IE;
        harmony_load(s);
        harmony_update_irq(s);
        harmony_update_active(s);
        break;
    case HARMONY_OV:
        s->ov = val & OV_OV;
        break;
    case HARMONY_PIO:
        s->pio = val & PIO_PO;
        break;
    case HARMONY_ID:
    case HARMONY_PCURADD:
    case HARMONY_RCURADD:
    case HARMONY_DIAG:
        qemu_log_mask(LOG_GUEST_ERROR, "harmony: write to read-only register "
                      "%s\n", harmony_reg_name(addr));
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "harmony: write to unknown register "
                      "0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static const MemoryRegionOps harmony_ops = {
    .read = harmony_read,
    .write = harmony_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void harmony_reset_hold(Object *obj, ResetType type)
{
    HarmonyState *s = HARMONY(obj);

    s->reset = 0;
    s->cntl = 0x08;     /* 8 kHz, S16BE, mono */
    s->gainctl = 0;
    s->pnxtadd = 0;
    s->rnxtadd = 0;
    s->dstatus = 0;
    s->ov = 0;
    s->pio = 0;
    s->pcuradd = 0;
    s->poff = 0;
    s->rcuradd = 0;
    s->roff = 0;
    s->pcur_valid = false;
    s->pnxt_valid = false;
    s->rcur_valid = false;
    s->rnxt_valid = false;

    harmony_update_irq(s);
    harmony_open_voices(s);
    harmony_set_voices(s, false, false);
    s->rec_seen = false;
    s->starved = false;
    s->idle_ticks = 0;
}

static int harmony_post_load(void *opaque, int version_id)
{
    HarmonyState *s = opaque;

    s->freq = 0;
    harmony_open_voices(s);
    if (s->voice_out) {
        audio_be_set_active_out(s->audio_be, s->voice_out, false);
    }
    if (s->voice_in) {
        audio_be_set_active_in(s->audio_be, s->voice_in, false);
    }
    s->out_active = false;
    s->in_active = false;
    s->rec_seen = false;
    s->starved = false;
    s->idle_ticks = 0;
    harmony_update_active(s);
    qemu_set_irq(s->irq, s->irq_level);
    return 0;
}

static const VMStateDescription vmstate_harmony = {
    .name = "harmony",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = harmony_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(reset, HarmonyState),
        VMSTATE_UINT32(cntl, HarmonyState),
        VMSTATE_UINT32(gainctl, HarmonyState),
        VMSTATE_UINT32(pnxtadd, HarmonyState),
        VMSTATE_UINT32(rnxtadd, HarmonyState),
        VMSTATE_UINT32(dstatus, HarmonyState),
        VMSTATE_UINT32(ov, HarmonyState),
        VMSTATE_UINT32(pio, HarmonyState),
        VMSTATE_UINT32(pcuradd, HarmonyState),
        VMSTATE_UINT32(poff, HarmonyState),
        VMSTATE_UINT32(rcuradd, HarmonyState),
        VMSTATE_UINT32(roff, HarmonyState),
        VMSTATE_BOOL(pcur_valid, HarmonyState),
        VMSTATE_BOOL(pnxt_valid, HarmonyState),
        VMSTATE_BOOL(rcur_valid, HarmonyState),
        VMSTATE_BOOL(rnxt_valid, HarmonyState),
        VMSTATE_INT32(irq_level, HarmonyState),
        VMSTATE_END_OF_LIST()
    }
};

static void harmony_realize(DeviceState *dev, Error **errp)
{
    HarmonyState *s = HARMONY(dev);
    int i;

    /*
     * Output attenuation is 1.5 dB per step.  HP-UX puts its 0 dB play gain
     * at attenuation 6 (range -84..+9 dB), so that step is unity here and
     * the steps below it amplify, saturating at full scale.
     */
    for (i = 0; i < 64; i++) {
        s->out_att[i] = lrint(32768.0 *
                              pow(10.0, -1.5 * (i - s->out_unity_att) / 20.0));
    }

    if (!audio_be_check(&s->audio_be, errp)) {
        return;
    }

    harmony_open_voices(s);
    if (!s->voice_out) {
        error_setg(errp, "harmony: initializing audio output failed");
        return;
    }
}

static void harmony_unrealize(DeviceState *dev)
{
    HarmonyState *s = HARMONY(dev);

    if (s->voice_in) {
        audio_be_close_in(s->audio_be, s->voice_in);
        s->voice_in = NULL;
    }
    if (s->voice_out) {
        audio_be_close_out(s->audio_be, s->voice_out);
        s->voice_out = NULL;
    }
}

static void harmony_init(Object *obj)
{
    HarmonyState *s = HARMONY(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &harmony_ops, s, "harmony",
                          HARMONY_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static const Property harmony_properties[] = {
    DEFINE_AUDIO_PROPERTIES(HarmonyState, audio_be),
    /* revision 0x14 = no teleshare, 0x15 = teleshare */
    DEFINE_PROP_UINT32("id", HarmonyState, id, 0x00140000),
    DEFINE_PROP_UINT8("codec-rev", HarmonyState, codec_rev, 0x02),
    DEFINE_PROP_UINT8("out-unity-att", HarmonyState, out_unity_att, 6),
};

static void harmony_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    harmony_init_tables();

    dc->realize = harmony_realize;
    dc->unrealize = harmony_unrealize;
    dc->vmsd = &vmstate_harmony;
    dc->desc = "HP Harmony audio";
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    device_class_set_props(dc, harmony_properties);
    rc->phases.hold = harmony_reset_hold;
}

static const TypeInfo harmony_info = {
    .name          = TYPE_HARMONY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HarmonyState),
    .instance_init = harmony_init,
    .class_init    = harmony_class_init,
};

static void harmony_register_types(void)
{
    type_register_static(&harmony_info);
}

type_init(harmony_register_types)
