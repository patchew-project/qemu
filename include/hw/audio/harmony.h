/*
 * HP Harmony audio (LASI audio function, CS4215/AD1849 codec)
 *
 * Copyright (c) 2026 Keith Monahan <keith@techtravels.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_AUDIO_HARMONY_H
#define HW_AUDIO_HARMONY_H

#include "hw/core/sysbus.h"
#include "qemu/audio.h"
#include "qom/object.h"

#define TYPE_HARMONY "harmony"
OBJECT_DECLARE_SIMPLE_TYPE(HarmonyState, HARMONY)

#define HARMONY_MMIO_SIZE       0x1000
#define HARMONY_PAGE_SIZE       4096
#define HARMONY_MIX_FRAMES      1024
#define HARMONY_IN_RING         8192    /* frames, power of 2 */

struct HarmonyState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    AudioBackend *audio_be;
    SWVoiceOut *voice_out;
    SWVoiceIn *voice_in;

    /* properties */
    uint32_t id;
    uint8_t codec_rev;
    uint8_t out_unity_att;

    /* registers */
    uint32_t reset;
    uint32_t cntl;
    uint32_t gainctl;
    uint32_t pnxtadd;
    uint32_t rnxtadd;
    uint32_t dstatus;       /* IE only, PN/RN are below */
    uint32_t ov;
    uint32_t pio;

    /* DMA engine, playback and record share the codec sample clock */
    uint32_t pcuradd;
    uint32_t poff;
    uint32_t rcuradd;
    uint32_t roff;
    bool pcur_valid;
    bool pnxt_valid;        /* PN = !pnxt_valid */
    bool rcur_valid;
    bool rnxt_valid;        /* RN = !rnxt_valid */

    int irq_level;
    int freq;
    int32_t out_att[64];    /* Q15 per attenuation step */

    int16_t out_buf[HARMONY_MIX_FRAMES * 2];
    int16_t in_buf[HARMONY_MIX_FRAMES * 2];
    /* host input captured by the input voice, consumed at the codec clock */
    int16_t in_ring[HARMONY_IN_RING * 2];
    uint32_t in_rd;
    uint32_t in_wr;
    bool out_active;
    bool in_active;
    bool rec_seen;
    bool starved;
    int idle_ticks;
    uint8_t dma_buf[HARMONY_MIX_FRAMES * 4];
};

#endif
