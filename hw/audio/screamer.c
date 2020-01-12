/*
 * File: Screamer.c
 * Description: Implement the Screamer sound chip used in Apple Macintoshes.
 * It works by filling a buffer, then playing the buffer.
 */

#include "qemu/osdep.h"
#include "audio/audio.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include <inttypes.h>
#include "hw/ppc/mac.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "include/hw/audio/screamer.h"

#define DEBUG_SCREAMER 0
#define DPRINTF(fmt, ...) \
do { if (DEBUG_SCREAMER) { printf(fmt , ## __VA_ARGS__); } } while (0)

#define SOUND_CONTROL_REG  0
#define CODEC_CONTROL_REG  1
#define CODEC_STATUS_REG   2
#define CLIP_COUNT_REG     3
#define BYTE_SWAP_REG      4
#define FRAME_COUNT_REG    5

#define AWACS_BUSY         0x01000000

/* Used with AWACS register 1 */
#define RECALIBRATE         0x004
#define LOOPTHRU            0x040
#define SPEAKER_MUTE        0x080
#define HEADPHONE_MUTE      0x200
#define OUTPUT_ZERO         0x400
#define OUTPUT_ONE          0x800
#define PARALLEL_OUTPUT     0xc00

/* Function prototypes */
static uint32_t set_busy_bit(uint32_t value, int bit);
static uint32_t set_part_ready_bit(uint32_t value, int bit_value);
static uint32_t set_revision(uint32_t input_value);
static uint32_t set_manufacturer(uint32_t input_value);
static int get_sampling_rate(ScreamerState *s);
static uint32_t get_frame_count_reg(ScreamerState *s);
static void add_to_speaker_buffer(DBDMA_io *io);
static void dma_request(DBDMA_io *io);


/**************************** Getters *************************/

/* Returns the codec control register's encoded AWACS address */
static uint8_t get_codec_control_address(uint32_t value)
{
    uint8_t return_value;
    return_value = (value >> 12) & 0x00000fff;
    return return_value;
}


static uint32_t get_sound_control_reg(ScreamerState *s)
{
    DPRINTF("%s() called - returned 0x%x\n", __func__, s->sound_control);
    return s->sound_control;
}

/* The AWACS registers are accessed thru this register */
static uint32_t get_codec_control_reg(ScreamerState *s)
{
    int awacs_register = get_codec_control_address(s->codec_control);
    uint32_t return_value = s->awacs[awacs_register];
    return_value = set_busy_bit(return_value, 0); /* Tell CPU we are ready */
    DPRINTF("%s() called - returned 0x%x\tAWACS register: %d\n", __func__,
            return_value, awacs_register);
    return return_value;
}

/*
 * Determines if the readback bit is set.
 * It is used by the Codec Control register.
 */
static bool readback_enabled(ScreamerState *s)
{
/* Note: bit zero is the readback enabled bit */
    if (s->awacs[7] & 1) {
        return true;
    } else {
        return false;
    }
}

static uint32_t get_codec_status_reg(ScreamerState *s)
{
    uint32_t return_value;

    /* if in readback mode - return AWACS register value */
    if (readback_enabled(s)) {
        int awacs_register = (s->awacs[7] & 0xe) >> 1;
        s->awacs[7] = s->awacs[7] & 0xfffffffe; /* turn off readback mode */
        return_value = s->awacs[awacs_register] << 4;
        DPRINTF("readback enable bit is set, returning AWACS register %d\t"
                "value:0x%x\n", awacs_register, return_value);

        return return_value;
    }

    /* Tell CPU we are ready */
    return_value = set_part_ready_bit(s->codec_status, 1);

    /* Set Revision to Screamer */
    return_value = set_revision(return_value);

    /* Set the Manufacturer to Crystal */
    return_value = set_manufacturer(return_value);
    DPRINTF("%s() called - returned 0x%x\n", __func__, return_value);

    return return_value;
}

static uint32_t get_clip_count_reg(ScreamerState *s)
{
    DPRINTF("%s() called - returned 0x%x\n", __func__, s->clip_count);
    uint32_t return_value;
    return_value = s->clip_count;
    /* This is reset everytime it is read */
    s->clip_count = 0;
    return return_value;
}

static uint32_t get_byte_swap_reg(ScreamerState *s)
{
    DPRINTF("%s() called - returned 0x%x\n", __func__, s->byte_swap);
    /*
     * If all you hear is noise, it could be this register reporting the
     * wrong value.
     */
    return s->byte_swap ? 0 : 1;
}

/*
 * Returns the frame (sample) count
 */
static uint32_t get_frame_count_reg(ScreamerState *s)
{
    DPRINTF("%s() called - returned 0x%x\n", __func__, s->frame_count);
    return s->frame_count;
}

static uint8_t get_left_vol(uint32_t value)
{
    return value & 0xf;
}

static uint8_t get_right_vol(uint32_t value)
{
    return value & 0x3c0 >> 6;
}

/*
 * Returns the sampling rate.
 * If the audio is playing back too fast or too slow, this function may be the
 * cause.
 */
static int get_sampling_rate(ScreamerState *s)
{
    uint32_t screamer_rate = s->sound_control & 0x700;
    int return_value;

    /* All return values are in Hertz */
    switch (screamer_rate) {
    case 0x0:
        return_value = 44100;
        break;
    case 0x100:
        return_value = 29400;
        break;
    case 0x200:
        return_value = 22050;
        break;
    case 0x300:
        return_value = 17640;
        break;
    case 0x400:
        return_value = 14700;
        break;
    case 0x500:
        return_value = 11025;
        break;
    case 0x600:
        return_value = 8820;
        break;
    case 0x700:
        return_value = 7350;
        break;
    default:
        DPRINTF("get_sampling_rate() unknown value: 0x%x\nDefaulting to"
                " 44100 Hz.\n", screamer_rate);
        return 44100;
}
    DPRINTF("%s() called - returning %dHz\n", __func__, return_value);
    return return_value;
}

/**************************** End of getters *************************/

/***************************** Speaker call back *************************/

/* resets the play and buffer position markers */
static void reset_markers(ScreamerState *s)
{
    s->spk_play_position = 0;
    s->spk_buffer_position = 0;
}


/* Sends the samples to the host for playing */
static void send_samples_to_host(ScreamerState *s, int max_samples)
{
    int write_length, requested_length;
    requested_length = MIN(max_samples, (s->spk_buffer_position -
                                         s->spk_play_position));
    write_length = AUD_write(s->speaker_voice,
                             &s->spk_buffer[s->spk_play_position],
                             requested_length);
    DPRINTF("requested length: %d\twrite length: %d\t",
            requested_length, write_length);
    s->spk_play_position += write_length;
    DPRINTF("AUD_write %d/%d\n", s->spk_play_position, s->spk_buffer_position);
    s->frame_count += write_length;
}


/*
 * Called by QEMU's audio system to tell the output backend to send samples
 * from the buffer to the host sound system.
 * opaque: a pointer to the ScreamerState instance.
 * max_samples: the number of samples that can be sent to the hardware buffer.
 */
static void speaker_callback(void *opaque, int max_samples)
{
    ScreamerState *s = (ScreamerState *) opaque;

    /* if we have more samples to play */
    if (s->spk_buffer_position > 0) {
        if (s->spk_buffer_position > s->spk_play_position) {
            DPRINTF("%s() called - max_samples: %d\n", __func__, max_samples);
            send_samples_to_host(s, max_samples);
        }
        if (s->spk_play_position >= s->spk_buffer_position) {
            DPRINTF("done playing buffer\n");
            DPRINTF("pp: %d\tbp: %d\n", s->spk_play_position,
                    s->spk_buffer_position);
            if (s->spk_play_position > s->spk_buffer_position) {
                DPRINTF("Error detected! - pp > bp\n\a");
            }
            reset_markers(s);
            /* play postponed samples */
            if (s->dma_io.len > 0) {
                DPRINTF("playing postponed samples\n");
                add_to_speaker_buffer(&s->dma_io);
                return;
            }
        }
    }
}

/************************* End of speaker call back *************************/


/* Opens the speaker's voice */
static void open_speaker_voice(ScreamerState *s)
{
    DPRINTF("%s() called\n", __func__);

    /* if voice is already open return from function */
    if (s->speaker_voice != NULL) {
        DPRINTF("closing speaker voice\n");
        AUD_close_out(&s->card, s->speaker_voice);
        s->speaker_voice = NULL;
    }
    struct audsettings audio_settings;
    audio_settings.freq = get_sampling_rate(s);  /* in hertz */
    audio_settings.nchannels = 2;                /* stereo output */
    audio_settings.fmt = AUDIO_FORMAT_S16;       /* signed 16 bit */
    audio_settings.endianness = get_byte_swap_reg(s); /* endianness */
    s->speaker_voice = AUD_open_out(&s->card, s->speaker_voice, SOUND_CHIP_NAME
                                    " speaker", s, speaker_callback,
                                    &audio_settings);
    if (!s->speaker_voice) {
        AUD_log(SOUND_CHIP_NAME, "Out voice could not be opened\n");
    } else {
        AUD_set_active_out(s->speaker_voice, true);
    }
}


/******************************* Setters *************************************/


/* Updates QEMU's audio backend settings */
static void set_QEMU_audio_settings(ScreamerState *s)
{
    DPRINTF("%s() called\n", __func__);
    open_speaker_voice(s);
}


/* Return value: 1 = muted  0 = not muted */
static int is_muted(ScreamerState *s)
{
    int mute_state = s->awacs[1] & SPEAKER_MUTE ? 1 : 0;
    if (s->awacs[1] & SPEAKER_MUTE) {
        DPRINTF("speaker is muted\n");
    } else {
        DPRINTF("speaker is unmuted\n");
    }

    if (s->awacs[1] & HEADPHONE_MUTE) {
        DPRINTF("headphone is muted\n");
    } else {
        DPRINTF("headphone is unmuted\n");
    }
    return mute_state;
}


/* Converts Screamer's volume system to QEMU's system */
static int screamer_to_qemu_volume(int x)
{
    return -16 * x + 240;
}


/* Sets QEMU's volume. */
static void set_volume(ScreamerState *s)
{
    int should_mute = is_muted(s);

    /* Get Screamer volume values */
    uint8_t left_vol = get_left_vol(s->awacs[4]);
    uint8_t right_vol = get_right_vol(s->awacs[4]);
    DPRINTF("set_volume() called - M:%d\tL:%d\tR:%d\n", should_mute, left_vol,
            right_vol);

    /* Convert Screamer to QEMU volume values */
    left_vol = screamer_to_qemu_volume(left_vol);
    right_vol = screamer_to_qemu_volume(right_vol);
    DPRINTF("QEMU volume: L:%d\tR:%d\n", left_vol, right_vol);
    AUD_set_volume_out(s->speaker_voice, should_mute, left_vol, right_vol);
}


/* Sets the sound control register */
static void set_sound_control_reg(ScreamerState *s, uint32_t value)
{
    DPRINTF("set_sound_control_reg() called - value: 0x%x\n", value);
    s->sound_control = value;
    set_QEMU_audio_settings(s);
}


/* Used for input gain only - can be ignored for now. */
static void set_awacs_0_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("Settings AWACS register 0 to 0x%x\n", s->awacs[0]);
    s->awacs[0] = new_value;
}


static void set_awacs_1_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("Settings AWACS register 1 to 0x%x\n", new_value);

    s->awacs[1] = new_value;

    /* If recalibration requested */
    if (new_value & RECALIBRATE) {
        DPRINTF("Recalibration requested - unimplemented\n");
        new_value = new_value ^ RECALIBRATE; /* Turn off recalibrate bit */
    }

    /* If loop thru set - what does this mean? */
    if (new_value & LOOPTHRU) {
        DPRINTF("Loopthru enabled - doing nothing\n");
    }

    /* Set headphone jack mute state */
    if (new_value & HEADPHONE_MUTE) {
        DPRINTF("Headphone muted\n");
    }

    else {
        DPRINTF("Headphone unmuted\n");
    }

    if (new_value & SPEAKER_MUTE) {
        DPRINTF("Speaker muted\n");
    }

    else {
        DPRINTF("Speaker unmuted\n");
    }

    if (new_value & OUTPUT_ZERO) {
        DPRINTF("output zero set - not sure what this means\n");
    }

    if (new_value & OUTPUT_ONE) {
        DPRINTF("output one set - not sure what this means\n");
    }

    if (new_value & PARALLEL_OUTPUT) {
        DPRINTF("parallel port enabled - but no parallel port here\n");
    }

    set_volume(s);
}


/* This is used for headphone volume - not needed */
static void set_awacs_2_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("Settings AWACS register 2 to 0x%x\n"
            "Ignoring change in headphone volume.\n", s->awacs[2]);
    s->awacs[2] = new_value;
}


/* Unknown register purpose */
static void set_awacs_3_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("Settings AWACS register 3 to 0x%x\n"
            "This register has an unknown purpose and does not do anything\n",
            s->awacs[3]);
    s->awacs[3] = new_value;
}


/* Mostly deals with speaker volume */
static void set_awacs_4_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("AWACS register 4 write: 0x%x\n", new_value);
    s->awacs[4] = new_value;
    set_volume(s);
}


/* This register is about loop thru stuff I don't understand */
static void set_awacs_5_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("Settings AWACS register 5 to 0x%x\n"
            "Loop thru update ignored.\n", s->awacs[5]);
    s->awacs[5] = new_value;
}


/* Prints the states of the AWACS power register */
static void print_power_reg_values(uint32_t value)
{
    if ((value & 0x3) == 0) {
        printf("Screamer run state set\n");
    }
    if ((value & 0x3) == 1) {
        printf("Screamer doze state set\n");
    }
    if ((value & 0x3) == 2) {
        printf("Screamer idle state set\n");
    }
}


/* Power Magement register */
static void set_awacs_6_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("Settings AWACS register 6 to 0x%x\n"
            "Power management update ignored.\n", s->awacs[6]);
    if (DEBUG_SCREAMER) {
        print_power_reg_values(new_value);
    }
    s->awacs[6] = new_value;
}


/* Read Back - repeating something that was sent to this chip? */
static void set_awacs_7_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("Settings AWACS register 7 to 0x%x\n", new_value);
    s->awacs[7] = new_value;
}


/* Sets the AWACs registers - a.k.a. shadow registers */
static void set_awacs_register(ScreamerState *s, uint32_t value)
{
    int the_register = get_codec_control_address(value);

    switch (the_register) {
    case 0:
        set_awacs_0_reg(s, value);
        break;
    case 1:
        set_awacs_1_reg(s, value);
        break;
    case 2:
        set_awacs_2_reg(s, value);
        break;
    case 3:
        set_awacs_3_reg(s, value);
        break;
    case 4:
        set_awacs_4_reg(s, value);
        break;
    case 5:
        set_awacs_5_reg(s, value);
        break;
    case 6:
        set_awacs_6_reg(s, value);
        break;
    case 7:
        set_awacs_7_reg(s, value);
        break;
    default:
        DPRINTF("Unhandled awacs registers %d\n", the_register);
    }
}


/* Used to set the AWACS registers */
static void set_codec_control_reg(ScreamerState *s, uint32_t value)
{
    DPRINTF("set_codec_control_reg() called - value: 0x%x\n", value);
    s->codec_control = value;
    set_awacs_register(s, value);
}

static void set_codec_status_reg(ScreamerState *s, uint32_t value)
{
    DPRINTF("set_codec_status_reg() called - value: 0x%x\n", value);
    s->codec_status = value;
}

static void set_clip_count_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("set_clip_count_reg() called - value: 0x%x\n", new_value);
    s->clip_count = new_value;
}

static void set_byte_swap_reg(ScreamerState *s, uint32_t value)
{
    DPRINTF("set_byte_swap_reg() called - value: 0x%x\n", value);
    s->byte_swap = value;
}

static void set_frame_count_reg(ScreamerState *s, uint32_t new_value)
{
    DPRINTF("%s() called - value: 0x%x\n", __func__, new_value);
    s->frame_count = new_value;
}

/*
 * Sets the busy bit of codec control register.
 * It is used to tell the CPU to wait.
 * value: the codec control register's value
 * bit_value: used to set or disable the busy bit
 */
static uint32_t set_busy_bit(uint32_t value, int bit_value)
{
    const int busy_bit = 0x01000000;
    uint32_t return_value;
    if (bit_value == 1)  /* Set this bit */
        return_value = (value | busy_bit);
    else /* bit_value == 0  Disable this bit */
        return_value = (value & ~busy_bit);
    return return_value;
}


/*
 * Sets the part ready bit of the codec status register
 * value: the codec status register's value
 * bit_value: used to set or disable the part ready bit
 */
static uint32_t set_part_ready_bit(uint32_t value, int bit_value)
{
    const int part_ready_bit = 0x00400000;
    uint32_t return_value;
    if (bit_value == 1)  /* Set this bit */
        return_value = (value | part_ready_bit);
    else /* bit_value == 0  Disable this bit */
        return_value = (value & ~part_ready_bit);
    return return_value;
}

/* Sets bits 12 and 13 to 1 to indicate the Screamer revision */
static uint32_t set_revision(uint32_t input_value)
{
    uint32_t return_value;
    return_value = input_value | 0x3000;
    return return_value;
}

/* Sets bit 8 to indicate Crystal as the manufacturer */
static uint32_t set_manufacturer(uint32_t input_value)
{
    uint32_t return_value;
    return_value = input_value | 0x100;
    return return_value;
}


/************************** End of Setters *********************************/


/*************************** DMA functions *********************************/

/*
 * Sends audio samples from a microphone or line-in to memory.
 * Used for sound input.
 * Currently only prevents a deadlock condition with Mac OS 9.
 */
static void screamer_to_dma(DBDMA_io *io)
{
    DPRINTF("%s() called\n", __func__);
    ScreamerState *s = (ScreamerState *)io->opaque;
    DBDMAState *dbs = s->dbdma;
    DBDMA_channel *ch = &dbs->channels[0x12];
    ch->regs[DBDMA_STATUS] |= DEAD;
    ch->regs[DBDMA_STATUS] &= ~ACTIVE;
    io->dma_end(io);
    return;
}


static void print_dma_info(DBDMA_io *io)
{
    #define RUN        0x8000
    #define PAUSE      0x4000
    #define FLUSH      0x2000
    #define WAKE       0x1000
    #define DEAD       0x0800
    #define ACTIVE     0x0400
    #define BT         0x0100
    #define DEVSTAT    0x00ff

    /*
     * RUN and PAUSE are bits under software control only.
     * FLUSH and WAKE are set by SW and cleared by hardware.
     * DEAD, ACTIVE and BT are only under hardware control.
     */

    DBDMA_channel *ch = io->channel;
    printf("DMA FLAGS: ");

    if (ch->regs[DBDMA_STATUS] & RUN) {
        printf("RUN ");
    }

    if (ch->regs[DBDMA_STATUS] & ACTIVE) {
        printf("ACTIVE ");
    }

    if (ch->regs[DBDMA_STATUS] & PAUSE) {
        printf("PAUSE ");
    }

    if (ch->regs[DBDMA_STATUS] & DEAD) {
        printf("DEAD ");
    }

    if (ch->regs[DBDMA_STATUS] & WAKE) {
        printf("WAKE ");
    }

    if (ch->regs[DBDMA_STATUS] & BT) {
        printf("BT ");
    }

    if (ch->regs[DBDMA_STATUS] & DEVSTAT) {
        printf("DEVSTAT ");
    }

    if (ch->regs[DBDMA_STATUS] & FLUSH) {
        printf("FLUSH ");
    }

    if (ch->io.processing == true) {
        printf("processing  ");
    }

    printf("\n");
}

/* Tell the DMA controller we request more samples */
static void dma_request(DBDMA_io *io)
{
    DPRINTF("%s() called\n", __func__);
    if (DEBUG_SCREAMER) {
        print_dma_info(io);
    }
    io->len = 0;
    io->dma_end(io);
}


/* Adds sample data to the buffer */
static void add_to_speaker_buffer(DBDMA_io *io)
{
    ScreamerState *s = (ScreamerState *) io->opaque;

    if (s->spk_buffer_position + io->len > MAX_BUFFER_SIZE) {
        /* postpone calling these samples until the buffer has been emptied */
        memcpy(&s->dma_io, io, sizeof(DBDMA_io));
        return;
    }
    dma_memory_read(&address_space_memory, io->addr,
                    &s->spk_buffer[s->spk_buffer_position], io->len);
    s->spk_buffer_position += io->len;
    DPRINTF("%s() called - len: %d pos: %d/%d\n", __func__, io->len,
            s->spk_buffer_position, MAX_BUFFER_SIZE);

    dma_request(io);
}

/*
 * Called by the DMA chip to transfer samples from memory to the
 * Screamer chip.
 * Used for sound output.
 */
static void dma_to_screamer(DBDMA_io *io)
{
    add_to_speaker_buffer(io);
}


/*
 * This will flush the audio buffer of previous audio - eliminating previous
 * audio playback.
 */
static void send_silence_to_speaker(ScreamerState *s)
{
    DPRINTF("Silencing audio buffer...\n");
    int length = MAX_BUFFER_SIZE;
    s->spk_buffer_position = length;
    s->spk_play_position = 0;
    memset(s->spk_buffer, 0, length);
    s->dma_io.len = 0; /* stop any postponed samples from playing */
}


/* This is called after audio stops playing */
static void dma_send_flush(DBDMA_io *io)
{
    DPRINTF("dma_send_flush() called\n");
    if (DEBUG_SCREAMER) {
        print_dma_info(io);
    }
    ScreamerState *s = (ScreamerState *)io->opaque;
    reset_markers(s);
    send_silence_to_speaker(s);
    if (io->len > 0) {
        dma_request(io);
    }
}


static void dma_receive_flush(DBDMA_io *io)
{
    DPRINTF("dma_receive_flush() called\n");
}


/* Set the functions the DMA system will call */
void screamer_register_dma_functions(ScreamerState *s, void *dbdma,
                                   int send_channel, int receive_channel)
{
    DPRINTF("%s() called\n", __func__);
    DPRINTF("send channel: %d\treceive channel: %d\n", send_channel,
           receive_channel);
    s->dbdma = dbdma;

    /* Setup the DMA send system */
    DBDMA_register_channel(s->dbdma, send_channel, s->dma_send_irq,
                           dma_to_screamer, dma_send_flush, s);

    /* Setup the DMA receive system */
    DBDMA_register_channel(s->dbdma, receive_channel, s->dma_receive_irq,
                           screamer_to_dma, dma_receive_flush, s);
}

/************************* End of DMA functions **************************/

/* Resets this sound chip */
static void screamer_reset(DeviceState *d)
{
    DPRINTF("screamer_reset() called\n");
    ScreamerState *s = SCREAMER(d);
    set_sound_control_reg(s, 0);
    set_codec_control_reg(s, 0);
    set_codec_status_reg(s, 0);
    set_clip_count_reg(s, 0);
    set_byte_swap_reg(s, 0);
    set_frame_count_reg(s, 0);
    int i, num_awacs_regs = 8;
    for (i = 0; i < num_awacs_regs; i++) {
        s->awacs[i] = 0;
    }
    set_QEMU_audio_settings(s);
    reset_markers(s);
    s->dma_io.len = 0;
}

/* Called when the CPU reads the memory addresses assigned to Screamer */
static uint64_t screamer_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    ScreamerState *state = opaque;
    uint32_t return_value;

    addr = addr >> 4;
    switch (addr) {
    case SOUND_CONTROL_REG:
        return_value = get_sound_control_reg(state);
        break;
    case CODEC_CONTROL_REG:
        return_value = get_codec_control_reg(state);
        break;
    case CODEC_STATUS_REG:
        return_value = get_codec_status_reg(state);
        break;
    case CLIP_COUNT_REG:
        return_value = get_clip_count_reg(state);
        break;
    case BYTE_SWAP_REG:
        return_value = get_byte_swap_reg(state);
        break;
    case FRAME_COUNT_REG:
        return_value = get_frame_count_reg(state);
        break;
    default:
        DPRINTF("Unknown register read - addr:%llu\tsize:%d\n", addr, size);
        return_value = 12021981; /* Value used for debugging purposes */
    }
    DPRINTF("screamer_mmio_read() called addr: %llu  size: %d", addr >> 4,
            size);
    DPRINTF("  returning 0x%x\n", return_value);
    return return_value;
}


/* Called when the CPU writes to the memory addresses assigned to Screamer */
static void screamer_mmio_write(void *opaque, hwaddr addr, uint64_t raw_value,
                                unsigned size)
{
    DPRINTF("screamer_mmio_write() called - size: %d\n", size);
    ScreamerState *state = opaque;
    uint32_t value = raw_value & 0xffffffff;
    addr = addr >> 4;

    switch (addr) {
    case SOUND_CONTROL_REG:
        set_sound_control_reg(state, value);
        break;
    case CODEC_CONTROL_REG:
        set_codec_control_reg(state, value);
        break;
    case CODEC_STATUS_REG:
        set_codec_status_reg(state, value);
        break;
    case CLIP_COUNT_REG:
        set_clip_count_reg(state, value);
        break;
    case BYTE_SWAP_REG:
        set_byte_swap_reg(state, value);
        break;
    case FRAME_COUNT_REG:
        set_frame_count_reg(state, value);
        break;
    default:
        DPRINTF("Unknown register write - addr:%llu\tvalue:%d\n", addr, value);
    }
}

/* Used for memory_region_init_io() for memory mapped I/O */
static const MemoryRegionOps screamer_ops = {
    .read = screamer_mmio_read,
    .write = screamer_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

/* Called when the device has become active */
static void screamer_realize(DeviceState *dev, Error **errp)
{
    DPRINTF("screamer_realize() called\n");
    screamer_reset(dev);
}


/*
 * Called when an instance of the Screamer device is created.
 * Also called when this HMP command is called: device_add screamer
 */
static void screamer_init(Object *obj)
{
    DPRINTF("screamer_init() called\n");

    ScreamerState *s = (ScreamerState *)obj;
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    const int region_size = 5 * 32;

    /* Makes the read and write ops work */
    memory_region_init_io(&s->io_memory_region, OBJECT(s),
                          &screamer_ops, s, SOUND_CHIP_NAME, region_size);

    /* Sets the SysBusDevice's memory property */
    sysbus_init_mmio(d, &s->io_memory_region);

    /* Setup all the interrupt requests */
    sysbus_init_irq(d, &s->irq);
    sysbus_init_irq(d, &s->dma_send_irq);
    sysbus_init_irq(d, &s->dma_receive_irq);

    /* Registers Screamer with QEMU's audio system */
    AUD_register_card(SOUND_CHIP_NAME, &s->card);
}


/*
 * These properties are displayed when using this command in the HMP.
 * Command-line Example: --device screamer,help
 * HMP Example:          device-list-properties
 */
static Property screamer_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

/*
 * When saving and restoring the state of the VM, this is used to save and
 * restore the registers.
 */
static const VMStateDescription vmstate_screamer = {
    .name = "Screamer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16_ARRAY(awacs, ScreamerState, 8), /* 8 AWACS registers */
        VMSTATE_UINT32(sound_control, ScreamerState),
        VMSTATE_UINT32(codec_control, ScreamerState),
        VMSTATE_UINT32(codec_status, ScreamerState),
        VMSTATE_UINT32(clip_count, ScreamerState),
        VMSTATE_UINT32(byte_swap, ScreamerState),
        VMSTATE_UINT32(frame_count, ScreamerState),
        VMSTATE_END_OF_LIST()
    }
};


/*
 * Sets the class data. It is like polymorphism and inheritance in object
 * oriented languages.
 */
static void screamer_class_init(ObjectClass *class, void *data)
{
    DPRINTF("screamer_class_init() called\n");
    DeviceClass *dc = DEVICE_CLASS(class);
    dc->realize = screamer_realize;
    dc->reset = screamer_reset;
    dc->desc = "Apple Screamer";
    dc->props = screamer_properties;
    dc->vmsd = &vmstate_screamer;
    dc->hotpluggable = false;
}

/* Used for QOM function registration */
static const TypeInfo screamer_info = {
    .name          = "screamer",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ScreamerState),
    .instance_init = screamer_init,
    .class_init    = screamer_class_init,
};

/* QOM registration of above functions for calling */
static void screamer_register_types(void)
{
    DPRINTF("screamer_register_types() called\n");
    type_register_static(&screamer_info);
}

/* QEMU Object Model (QOM) stuff */
type_init(screamer_register_types)
