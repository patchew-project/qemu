/*
 * File: screamer.h
 * Description: header file to the hw/audio/screamer.c file
 */

#ifndef screamer_h
#define screamer_h

#include <inttypes.h>
#include "audio/audio.h"
#include "hw/ppc/mac_dbdma.h"

#define TYPE_SCREAMER "screamer"
#define SCREAMER(obj) OBJECT_CHECK(ScreamerState, (obj), TYPE_SCREAMER)
#define SOUND_CHIP_NAME "Screamer Sound Chip"
#define MAX_BUFFER_SIZE (128 * 64)

typedef struct ScreamerState {
    SysBusDevice parent_obj;
    uint16_t awacs[8]; /* Shadow/awacs registers */
    uint32_t sound_control;
    uint32_t codec_control;
    uint32_t codec_status;
    uint32_t clip_count;
    uint32_t byte_swap;
    uint32_t frame_count;
    SWVoiceOut *speaker_voice;
    DBDMAState *dbdma;
    qemu_irq dma_send_irq;
    qemu_irq dma_receive_irq;
    qemu_irq irq;
    QEMUSoundCard card;
    MemoryRegion io_memory_region;
    uint8_t spk_buffer[MAX_BUFFER_SIZE];
    uint16_t spk_buffer_position, spk_play_position;
    DBDMA_io dma_io;
} ScreamerState;

void screamer_register_dma_functions(ScreamerState *s, void *dbdma,
                                     int send_channel, int receive_channel);

#endif /* screamer_h */
