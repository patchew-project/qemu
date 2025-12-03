/*
 * QEMU Macintosh SuperDrive floppy disk drive emulator
 *
 * Copyright (c) 2025 Matt Jacobson <mhjacobson@me.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "qemu/error-report.h"
#include "hw/block/sony_superdrive.h"
#include "system/block-backend.h"
#include "system/block-backend-io.h"
#include "system/blockdev.h"
#include "trace.h"

/* Sony drive register selects (CA2-CA1-CA0-SEL) */
#define SONY_REG_DIRTN       0x0
#define SONY_REG_CSTIN       0x1 /* a.k.a. dipAdr ("disk in place") */
#define SONY_REG_STEP        0x2
#define SONY_REG_WRTPRT      0x3
#define SONY_REG_MOTORON     0x4
#define SONY_REG_TKO         0x5
#define SONY_REG_EJECT_L     0x6
#define SONY_REG_TACH        0x7
#define SONY_REG_RDDATA0     0x8
#define SONY_REG_RDDATA1     0x9
#define SONY_REG_SUPERDRIVE  0xa /* a.k.a. mfmDrvAdr */
#define SONY_REG_GCR         0xb
#define SONY_REG_SINGLE_SIDE 0xc
#define SONY_REG_READY       0xd
#define SONY_REG_INSTALLED   0xe /* a.k.a. DRVIN, a.k.a. drvExstAdr */
#define SONY_REG_TWOMEG      0xf

static const char *const sony_drive_reg_names[] = {
    [SONY_REG_DIRTN] = "DIRTN",
    [SONY_REG_CSTIN] = "CSTIN",
    [SONY_REG_STEP] = "STEP",
    [SONY_REG_WRTPRT] = "WRTPRT",
    [SONY_REG_MOTORON] = "MOTOR_ON",
    [SONY_REG_TKO] = "TKO",
    [SONY_REG_EJECT_L] = "EJECT_L",
    [SONY_REG_TACH] = "TACH",
    [SONY_REG_RDDATA0] = "RDDATA0",
    [SONY_REG_RDDATA1] = "RDDATA1",
    [SONY_REG_SUPERDRIVE] = "SUPERDRIVE",
    [SONY_REG_GCR] = "GCR",
    [SONY_REG_SINGLE_SIDE] = "SINGLE_SIDE",
    [SONY_REG_READY] = "READY",
    [SONY_REG_INSTALLED] = "INSTALLED",
    [SONY_REG_TWOMEG] = "TWOMEG",
};

/* Sony drive write commands (CA2-CA1-CA0-SEL) */
#define SONY_CMD_TRACK_POS   0x0
#define SONY_CMD_TRACK_NEG   0x8
#define SONY_CMD_TRACK_STEP  0x2
#define SONY_CMD_SET_MFM     0x3
#define SONY_CMD_SET_GCR     0xb
#define SONY_CMD_MOTOR_ON    0x4
#define SONY_CMD_MOTOR_OFF   0xc
#define SONY_CMD_EJECT       0xe

static const char *const sony_drive_cmd_names[] = {
    [SONY_CMD_TRACK_POS] = "TRACK_POS",
    [SONY_CMD_TRACK_NEG] = "TRACK_NEG",
    [SONY_CMD_TRACK_STEP] = "TRACK_STEP",
    [SONY_CMD_SET_MFM] = "SET_MFM",
    [SONY_CMD_SET_GCR] = "SET_GCR",
    [SONY_CMD_MOTOR_ON] = "MOTOR_ON",
    [SONY_CMD_MOTOR_OFF] = "MOTOR_OFF",
    [SONY_CMD_EJECT] = "EJECT",
};

#define SONY_GCR_TAG_LENGTH      12
#define SONY_GCR_DATA_LENGTH    512
#define SONY_GCR_SECTOR_LENGTH  (SONY_GCR_TAG_LENGTH + SONY_GCR_DATA_LENGTH)
#define SONY_GCR_ENCODED_LENGTH 699 /* i.e., sony_6and2_packed_len(524) */
#define SONY_GCR_SYNC_LEN         6
#define SONY_MFM_SECTOR_LENGTH  512

static uint8_t sony_drive_sectors_for_track(const SonyDrive *const drive,
                                            const uint8_t track)
{
    if (drive->sectors_per_track) {
        return drive->sectors_per_track;
    } else {
        /*
         * 400K/800K GCR media use zone recording with variable sectors per
         * track; each zone spans 16 tracks.
         */
        static const uint8_t gcr_zone_spt[] = { 12, 11, 10, 9, 8 };
        uint8_t zone = track / 16;

        if (zone >= ARRAY_SIZE(gcr_zone_spt)) {
            zone = ARRAY_SIZE(gcr_zone_spt) - 1;
        }

        return gcr_zone_spt[zone];
    }
}

static void bitset_set(uint8_t *const bitset, const unsigned int index,
                       const bool value) {
    const uint8_t mask = (uint8_t)(1u << (index & 0x7));

    if (value)
        bitset[index >> 3] |= mask;
    else
        bitset[index >> 3] &= (uint8_t)~mask;
}

static void bitset_set_range(uint8_t *const bitset, const unsigned int index,
                             const unsigned int length, const bool value) {
    for (unsigned int i = index; i < index + length; i++) {
        bitset_set(bitset, i, value);
    }
}

static bool bitset_get(const uint8_t *const bitset, const unsigned int index) {
    const uint8_t mask = (uint8_t)(1u << (index & 0x7));
    return (bitset[index >> 3] & mask) != 0;
}

static const uint8_t sony_gcr_encode_table[] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

/* 0xff is used as filler for bytes the decoder should never see. */
static const uint8_t sony_gcr_decode_table[] = {
    0x00, 0x01, 0xff, 0xff, 0x02, 0x03, 0xff, 0x04,
    0x05, 0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x07, 0x08, 0xff, 0xff, 0xff, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0xff, 0xff, 0x0e, 0x0f, 0x10, 0x11,
    0x12, 0x13, 0xff, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1b, 0xff, 0x1c,
    0x1d, 0x1e, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff,
    0x20, 0x21, 0xff, 0x22, 0x23, 0x24, 0x25, 0x26,
    0x27, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x29,
    0x2a, 0x2b, 0xff, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x31, 0x32, 0xff, 0xff, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0xff, 0x39, 0x3a, 0x3b, 0x3c, 0x3d,
    0x3e, 0x3f,
};

static uint8_t sony_gcr_encode_byte(const uint8_t value)
{
    assert((value & 0xc0) == 0);
    return sony_gcr_encode_table[value];
}

static uint8_t sony_gcr_decode_byte(const uint8_t value)
{
    assert(value & 0x80);
    const uint8_t result = sony_gcr_decode_table[value - 0x96];
    assert(result != 0xff);
    return result;
}

static void sony_gcr_encode_bytes(const uint8_t *const in, uint8_t *const out,
                                  const size_t len)
{
    for (size_t i = 0; i < len; i++) {
        out[i] = sony_gcr_encode_byte(in[i]);
    }
}

static void sony_gcr_decode_bytes(const uint8_t *const in, uint8_t *const out,
                                  const size_t len)
{
    for (size_t i = 0; i < len; i++) {
        out[i] = sony_gcr_decode_byte(in[i]);
    }
}

static size_t sony_6and2_packed_len(const size_t in_size) {
    const size_t groups = in_size / 3;
    const size_t rem = in_size % 3;
    return groups * 4 + (rem ? (rem + 1) : 0);
}

static void sony_6and2_pack(const uint8_t *in, const size_t in_size,
                            uint8_t *restrict out, const size_t out_size)
{
    assert(sony_6and2_packed_len(in_size) == out_size);

    for (size_t i = 0; i < in_size; i += 3) {
        /* Allow partial groupings. */
        const bool have_b = (i + 1) < in_size;
        const bool have_c = (i + 2) < in_size;

        const uint8_t a = *in++;
        const uint8_t b = have_b ? *in++ : 0;
        const uint8_t c = have_c ? *in++ : 0;

        /* High two bits (A7:6, B7:6, C7:6), then low six bits. */
        const uint8_t high  = (uint8_t)(((a & 0xC0u) >> 2) |
                                        ((b & 0xC0u) >> 4) |
                                        ((c & 0xC0u) >> 6));
        const uint8_t low_a = (uint8_t)(a & 0x3Fu);
        const uint8_t low_b = (uint8_t)(b & 0x3Fu);
        const uint8_t low_c = (uint8_t)(c & 0x3Fu);

        *out++ = high;
        *out++ = low_a;
        if (have_b) *out++ = low_b;
        if (have_c) *out++ = low_c;
    }
}

static void sony_6and2_unpack(const uint8_t *in, const size_t in_size,
                              uint8_t *restrict out, const size_t out_size)
{
    assert(sony_6and2_packed_len(out_size) == in_size);

    for (size_t i = 0; i < in_size; i += 4) {
        /* Allow partial groupings. */
        const bool have_b = (i + 2) < in_size;
        const bool have_c = (i + 3) < in_size;

        /* High two bits (A7:6, B7:6, C7:6), then low six bits. */
        const uint8_t high = *in++;
        const uint8_t low_a = *in++;
        const uint8_t low_b = have_b ? *in++ : 0;
        const uint8_t low_c = have_c ? *in++ : 0;

        const uint8_t a = (uint8_t)((high << 2) & 0xC0u) | low_a;
        const uint8_t b = (uint8_t)((high << 4) & 0xC0u) | low_b;
        const uint8_t c = (uint8_t)((high << 6) & 0xC0u) | low_c;

        *out++ = a;
        if (have_b) *out++ = b;
        if (have_c) *out++ = c;
    }
}

static void sony_checksum_encode(const uint8_t *in, const size_t in_size,
                                 uint8_t *out, const size_t out_size,
                                 uint8_t *sum_out)
{
    assert(in_size == out_size);

    uint16_t sum_a = 0, sum_b = 0, sum_c = 0;

    for (size_t i = 0; i < in_size; i += 3) {
        const bool have_b = (i + 1) < in_size;
        const bool have_c = (i + 2) < in_size;

        const uint8_t a = *in++;
        const uint8_t b = have_b ? *in++ : 0;
        const uint8_t c = have_c ? *in++ : 0;

        /* Per-group left-rotate on sum_c. */
        sum_c = (uint16_t)((sum_c & 0x00FFu) << 1);
        if (sum_c >= 0x0100u) { sum_c++; } /* insert 1 on carry */

        /* Add to sum_a; carry from sum_c bumps sum_a. */
        sum_a += a;
        if (sum_c >= 0x0100u) { sum_a++; sum_c &= 0x00FFu; }
        const uint8_t a2 = a ^ (uint8_t)sum_c;

        /* Add to sum_b; carry from sum_a bumps sum_b. */
        if (have_b) {
            sum_b += b;
            if (sum_a >= 0x0100u) { sum_b++; sum_a &= 0x00FFu; }
        }
        const uint8_t b2 = b ^ (uint8_t)sum_a;

        /* Add to sum_c; carry from sum_b bumps sum_c. */
        if (have_c) {
            sum_c += c;
            if (sum_b >= 0x0100u) { sum_c++; sum_b &= 0x00FFu; }
        }
        const uint8_t c2 = c ^ (uint8_t)sum_b;

        *out++ = a2;
        if (have_b) *out++ = b2;
        if (have_c) *out++ = c2;
    }

    *sum_out++ = (uint8_t)sum_a;
    *sum_out++ = (uint8_t)sum_b;
    *sum_out++ = (uint8_t)sum_c;
}

static bool sony_checksum_decode(const uint8_t *in, const size_t in_size,
                                 uint8_t *out, const size_t out_size,
                                 const uint8_t *const sum)
{
    assert(in_size == out_size);

    uint16_t sum_a = 0, sum_b = 0, sum_c = 0;

    for (size_t i = 0; i < in_size; i += 3) {
        const bool have_b = (i + 1) < in_size;
        const bool have_c = (i + 2) < in_size;

        const uint8_t a2 = *in++;
        const uint8_t b2 = have_b ? *in++ : 0;
        const uint8_t c2 = have_c ? *in++ : 0;

        /* Per-group left-rotate on sum_c. */
        sum_c = (uint16_t)((sum_c & 0x00FFu) << 1);
        if (sum_c >= 0x0100u) { sum_c++; } /* insert 1 on carry */

        /* Add to sum_a; carry from sum_c bumps sum_a. */
        const uint8_t a = a2 ^ (uint8_t)sum_c;
        sum_a += a;
        if (sum_c >= 0x0100u) { sum_a++; sum_c &= 0x00FFu; }

        /* Add to sum_b; carry from sum_a bumps sum_b. */
        const uint8_t b = b2 ^ (uint8_t)sum_a;
        if (have_b) {
            sum_b += b;
            if (sum_a >= 0x0100u) { sum_b++; sum_a &= 0x00FFu; }
        }

        /* Add to sum_c; carry from sum_b bumps sum_c. */
        const uint8_t c = c2 ^ (uint8_t)sum_b;
        if (have_c) {
            sum_c += c;
            if (sum_b >= 0x0100u) { sum_c++; sum_b &= 0x00FFu; }
        }

        *out++ = a;
        if (have_b) *out++ = b;
        if (have_c) *out++ = c;
    }

    if (sum) {
        return sum[0] == (uint8_t)sum_a &&
               sum[1] == (uint8_t)sum_b &&
               sum[2] == (uint8_t)sum_c;
    } else {
        return false;
    }
}

static bool sony_validate_address(SonyDrive *const drive, const uint8_t track,
                                  const uint8_t head, const uint8_t sector)
{
    const uint8_t sectors = sony_drive_sectors_for_track(drive, track);
    assert(sectors > 0);

    if (track >= drive->cylinders) {
        return false;
    } else if (head > 0 && !drive->double_sided) {
        return false;
    } else if (sector >= sectors) {
        return false;
    } else {
        return true;
    }
}

static uint64_t sony_lba(SonyDrive *const drive, const uint8_t track,
                         const uint8_t head, const uint8_t sector)
{
    const uint8_t heads = drive->double_sided ? 2 : 1;

    if (drive->sectors_per_track) {
        return ((uint64_t)track * heads + head)
                * drive->sectors_per_track + sector;
    } else {
        uint64_t lba = 0;

        for (uint8_t i = 0; i < track; i++) {
            const uint8_t sectors = sony_drive_sectors_for_track(drive, i);
            assert(sectors > 0);
            lba += sectors * heads;
        }

        const uint8_t sectors = sony_drive_sectors_for_track(drive, track);
        assert(sectors > 0);
        lba += sectors * head;

        lba += sector;

        return lba;
    }
}

static bool sony_drive_prepare_gcr(SonyDrive *const drive, const uint64_t lba,
                                   const uint8_t track, const uint8_t head,
                                   const uint8_t sector)
{
    assert(head < 2);
    const uint8_t format_byte = drive->double_sided ? 0x22 : 0x2;

    uint8_t header_vals[5];
    header_vals[0] = track & 0x3f;
    header_vals[1] = sector;
    header_vals[2] = (head << 5) | ((track >> 6) & 0x1f);
    header_vals[3] = format_byte;
    header_vals[4] = header_vals[0] ^ header_vals[1] ^
                     header_vals[2] ^ header_vals[3];

    uint8_t sector_data[SONY_GCR_SECTOR_LENGTH];
    size_t sector_data_pos = 0;
    sector_data[sector_data_pos++] = track;
    sector_data[sector_data_pos++] = head;
    sector_data[sector_data_pos++] = sector;
    sector_data[sector_data_pos++] = format_byte;
    sector_data[sector_data_pos++] = 0x00; /* reserved */
    sector_data[sector_data_pos++] = 0x00;
    sector_data[sector_data_pos++] = (lba >> 8) & 0xff;
    sector_data[sector_data_pos++] = lba & 0xff;
    sector_data[sector_data_pos++] = 0x00;
    sector_data[sector_data_pos++] = 0x00;
    sector_data[sector_data_pos++] = 0x00;
    sector_data[sector_data_pos++] = 0x00;

    const int ret = blk_pread(drive->blk, lba * 512, SONY_GCR_DATA_LENGTH,
                              sector_data + sector_data_pos, 0);
    if (ret < 0) {
        return false;
    }
    sector_data_pos += SONY_GCR_DATA_LENGTH;
    assert(sector_data_pos == sizeof sector_data);

    /* Fill drive->xfer_buffer. */
    size_t idx = 0;

    /*
     * Sync field.  Note that we don't actually have to replicate the sync bytes
     * as on disk; we can just return what the IWM/SWIM would see.
     * We use a six-byte field since that matches the size of what the CPU
     * will write.
     */
    memset(&drive->xfer_buffer[idx], 0xff, SONY_GCR_SYNC_LEN);
    idx += SONY_GCR_SYNC_LEN;

    drive->xfer_buffer[idx++] = 0xd5;
    drive->xfer_buffer[idx++] = 0xaa;
    drive->xfer_buffer[idx++] = 0x96;

    sony_gcr_encode_bytes(header_vals, &drive->xfer_buffer[idx],
                          sizeof header_vals);
    idx += sizeof header_vals;

    drive->xfer_buffer[idx++] = 0xde;
    drive->xfer_buffer[idx++] = 0xaa;

    drive->xfer_write_position = idx;

    /* Sync field. */
    memset(&drive->xfer_buffer[idx], 0xff, SONY_GCR_SYNC_LEN);
    idx += SONY_GCR_SYNC_LEN;

    drive->xfer_buffer[idx++] = 0xd5;
    drive->xfer_buffer[idx++] = 0xaa;
    drive->xfer_buffer[idx++] = 0xad;
    drive->xfer_buffer[idx++] = sony_gcr_encode_byte(sector);

    /* Sector data. */
    uint8_t sector_checksum[3];
    sony_checksum_encode(sector_data, sizeof sector_data,
                         sector_data, sizeof sector_data, sector_checksum);
    sony_6and2_pack(sector_data, sizeof sector_data,
                    drive->xfer_buffer + idx, SONY_GCR_ENCODED_LENGTH);
    sony_gcr_encode_bytes(drive->xfer_buffer + idx,
                          drive->xfer_buffer + idx, SONY_GCR_ENCODED_LENGTH);
    idx += SONY_GCR_ENCODED_LENGTH;

    /* Encode and append checksum. */
    sony_6and2_pack(sector_checksum, sizeof sector_checksum,
                    drive->xfer_buffer + idx, 4);
    sony_gcr_encode_bytes(drive->xfer_buffer + idx,
                          drive->xfer_buffer + idx, 4);
    idx += 4;

    /* Trail marks. */
    drive->xfer_buffer[idx++] = 0xde;
    drive->xfer_buffer[idx++] = 0xaa;

    /* Add some empty space; the Mac driver overwrites this area. */
    drive->xfer_buffer[idx++] = 0xff;
    drive->xfer_buffer[idx++] = 0xff;
    drive->xfer_buffer[idx++] = 0xff;
    drive->xfer_buffer[idx++] = 0xff;

    drive->xfer_length = idx;
    drive->xfer_position = 0;
    drive->xfer_active = true;

    return true;
}

static bool sony_drive_persist_gcr(SonyDrive *const drive)
{
    uint8_t encoded_data[SONY_GCR_ENCODED_LENGTH];
    uint8_t sector_data[SONY_GCR_SECTOR_LENGTH];

    uint8_t *const encoded_buffer = drive->xfer_buffer + SONY_GCR_SYNC_LEN + 3
                                     + 5 + 2 + SONY_GCR_SYNC_LEN + 4;
    sony_gcr_decode_bytes(encoded_buffer, encoded_data,
                          SONY_GCR_ENCODED_LENGTH);
    sony_6and2_unpack(encoded_data, sizeof encoded_data,
                      sector_data, sizeof sector_data);

    uint8_t encoded_checksum[4];
    uint8_t checksum[3];
    sony_gcr_decode_bytes(encoded_buffer + SONY_GCR_ENCODED_LENGTH,
                          encoded_checksum, 4);
    sony_6and2_unpack(encoded_checksum, sizeof encoded_checksum,
                      checksum, sizeof checksum);
    const bool result = sony_checksum_decode(sector_data, sizeof sector_data,
                                             sector_data, sizeof sector_data,
                                             checksum);

    if (!result) {
        return false;
    } else {
        const int ret = blk_pwrite(drive->blk, drive->xfer_lba * 512,
                                   SONY_GCR_DATA_LENGTH,
                                   &sector_data[SONY_GCR_TAG_LENGTH], 0);
        return ret >= 0;
    }
}

static bool sony_drive_prepare_mfm(SonyDrive *const drive, const uint64_t lba,
                                   const uint8_t track, const uint8_t head,
                                   const uint8_t sector)
{
    size_t idx = 0;

    /* Sync field. */
    memset(drive->xfer_buffer + idx, 0x00, 12);
    idx += 12;

    /* Address field. */
    bitset_set_range(drive->xfer_mark_bitset, idx, 3, true);
    drive->xfer_buffer[idx++] = 0xA1;
    drive->xfer_buffer[idx++] = 0xA1;
    drive->xfer_buffer[idx++] = 0xA1;

    drive->xfer_buffer[idx++] = 0xFE;
    drive->xfer_buffer[idx++] = track;
    drive->xfer_buffer[idx++] = head;
    drive->xfer_buffer[idx++] = sector + 1; /* NOTE: 1-indexed, unlike GCR! */
    drive->xfer_buffer[idx++] = 0x02; /* 512 bytes/sector */
    drive->xfer_buffer[idx++] = 0x00; /* CRC (unimplemented) */
    drive->xfer_buffer[idx++] = 0x00; /* CRC (unimplemented) */

    /* Intra-sector gap. */
    memset(drive->xfer_buffer + idx, 0x4E, 22);
    idx += 22;

    /* Sync field. */
    memset(drive->xfer_buffer + idx, 0x00, 12);
    idx += 12;

    /* Data mark. */
    bitset_set_range(drive->xfer_mark_bitset, idx, 3, true);
    drive->xfer_buffer[idx++] = 0xA1;
    drive->xfer_buffer[idx++] = 0xA1;
    drive->xfer_buffer[idx++] = 0xA1;

    drive->xfer_write_position = idx;
    drive->xfer_buffer[idx++] = 0xFB;

    /* Data. */
    const int ret = blk_pread(drive->blk, lba * 512, SONY_MFM_SECTOR_LENGTH,
                              drive->xfer_buffer + idx, 0);
    if (ret < 0) {
        return false;
    }
    idx += SONY_MFM_SECTOR_LENGTH;

    drive->xfer_buffer[idx++] = 0; /* CRC (unimplemented) */
    drive->xfer_buffer[idx++] = 0; /* CRC (unimplemented) */

    /* Inter-sector gap. */
    memset(drive->xfer_buffer + idx, 0x4E, 101);
    idx += 101;

    drive->xfer_length = idx;
    drive->xfer_position = 0;
    drive->xfer_active = true;

    return true;
}

static bool sony_drive_persist_mfm(SonyDrive *const drive)
{
    const size_t offset = 12 + 10 + 22 + 12 + 4;
    const int ret = blk_pwrite(drive->blk, drive->xfer_lba * 512,
                               SONY_MFM_SECTOR_LENGTH,
                               drive->xfer_buffer + offset, 0);
    return ret >= 0;
}

static void sony_drive_transfer_reset(SonyDrive *const drive)
{
    /* First, persist any dirty xfer. */
    if (drive->xfer_dirty) {
        bool result;

        if (drive->gcr_encode) {
            result = sony_drive_persist_gcr(drive);
        } else {
            result = sony_drive_persist_mfm(drive);
        }

        assert(result);
        trace_sony_drive_persist(drive, drive->xfer_lba);
    }

    drive->xfer_active = false;
    drive->xfer_dirty = false;
    drive->xfer_lba = UINT64_MAX;
    drive->xfer_position = 0;
    drive->xfer_write_position = 0;
    drive->xfer_length = 0;
    memset(drive->xfer_buffer, 0, sizeof drive->xfer_buffer);
    memset(drive->xfer_mark_bitset, 0, sizeof drive->xfer_mark_bitset);
}

static bool sony_drive_prepare(SonyDrive *const drive)
{
    if (!drive->disk_in) {
        trace_sony_drive_prepare(drive, 0, 0, 0, 0, 0);
        return false;
    }

    sony_drive_transfer_reset(drive);

    const uint8_t sectors = sony_drive_sectors_for_track(drive,
                                                         drive->current_track);
    const uint8_t head = drive->sel ? 1 : 0;
    const uint8_t track = drive->current_track;

    assert(head == 0 || drive->double_sided);
    assert(sectors != 0);

    const uint8_t sector = drive->current_sector;
    drive->current_sector = (drive->current_sector + 1) % sectors;
    assert(sony_validate_address(drive, track, head, sector));

    const uint64_t lba = sony_lba(drive, track, head, sector);
    assert(lba < drive->total_sectors);

    bool result;
    if (drive->gcr_encode) {
        result = sony_drive_prepare_gcr(drive, lba, track, head, sector);
    } else {
        result = sony_drive_prepare_mfm(drive, lba, track, head, sector);
    }

    drive->xfer_lba = lba;
    trace_sony_drive_prepare(drive, track, head, sector, drive->gcr_encode,
                             drive->xfer_length);
    return result;
}

static uint8_t sony_drive_selected_drive_reg(const SonyDrive *const drive)
{
    uint8_t selector = 0;

    if (drive->phases & 0x4) { /* CA2 */
        selector |= 0x8;
    }
    if (drive->phases & 0x2) { /* CA1 */
        selector |= 0x4;
    }
    if (drive->phases & 0x1) { /* CA0 */
        selector |= 0x2;
    }
    if (drive->sel) {
        selector |= 0x1;
    }

    return selector;
}

static void sony_drive_step(SonyDrive *const drive)
{
    if (drive->seek_direction < 0) {
        if (drive->current_track > 0) {
            drive->current_track--;
        }
    } else {
        if (!drive->cylinders || drive->current_track + 1 < drive->cylinders) {
            drive->current_track++;
        }
    }

    sony_drive_transfer_reset(drive);
    trace_sony_drive_step(drive, drive->seek_direction, drive->current_track);
}

static void sony_drive_strobe_drive(SonyDrive *const drive)
{
    const uint8_t selector = sony_drive_selected_drive_reg(drive);
    trace_sony_drive_strobe(drive, selector, sony_drive_cmd_names[selector]);

    switch (selector) {
    case SONY_CMD_TRACK_POS:
        drive->seek_direction = 1;
        break;
    case SONY_CMD_TRACK_NEG:
        drive->seek_direction = -1;
        break;
    case SONY_CMD_TRACK_STEP:
        sony_drive_step(drive);
        break;
    case SONY_CMD_SET_MFM:
        drive->use_gcr = false;
        break;
    case SONY_CMD_SET_GCR:
        drive->use_gcr = true;
        break;
    case SONY_CMD_MOTOR_ON:
        drive->motor_on = true;
        break;
    case SONY_CMD_MOTOR_OFF:
        drive->motor_on = false;
        sony_drive_transfer_reset(drive);
        break;
    case SONY_CMD_EJECT:
        blk_eject(drive->blk, true);
        drive->disk_in = false;
        sony_drive_reset(drive);
        break;
    default:
        break;
    }
}

bool sony_drive_read_sense(SonyDrive *const drive)
{
    const uint8_t reg = sony_drive_selected_drive_reg(drive);
    bool value;

    switch (reg) {
    case SONY_REG_DIRTN:
        /* "Direction". */
        value = drive->seek_direction < 0;
        break;
    case SONY_REG_CSTIN:
        /* "Cassette in". */
        value = !drive->disk_in; /* sense inverted */
        break;
    case SONY_REG_STEP:
        value = true; /* not emulated; our seeks are instantaneous */
        break;
    case SONY_REG_WRTPRT:
        value = !drive->write_protected; /* sense inverted */
        break;
    case SONY_REG_MOTORON:
        value = !drive->motor_on; /* sense inverted */
        break;
    case SONY_REG_TKO:
        /* "Track zero". */
        value = drive->current_track != 0; /* sense inverted */
        break;
    case SONY_REG_EJECT_L:
        /* Not pressing eject switch. */
        value = false;
        break;
    case SONY_REG_TACH:
        /* Tacho functionality not emulated. */
        value = !drive->motor_on;
        break;
    case SONY_REG_RDDATA0:
    case SONY_REG_RDDATA1:
        /* Single-bit reads are not emulated. */
        value = true;
        break;
    case SONY_REG_SUPERDRIVE:
        value = true;
        break;
    case SONY_REG_GCR:
        value = !drive->use_gcr; /* sense inverted */
        break;
    case SONY_REG_SINGLE_SIDE:
        /* We are a double-sided drive. */
        value = true; /* sense inverted */
        break;
    case SONY_REG_READY:
        value = !(drive->disk_in && drive->motor_on); /* sense inverted */
        break;
    case SONY_REG_INSTALLED:
        value = false; /* sense inverted */
        break;
    case SONY_REG_TWOMEG:
        /* "Two-meg" refers to the unformatted size of HD MFM floppies. */
        value = !(drive->hd_media && drive->disk_in); /* sense inverted */
        break;
    default:
        value = true;
        break;
    }

    trace_sony_drive_read_sense(drive, reg, sony_drive_reg_names[reg], value);
    return value;
}

void sony_drive_set_inputs(SonyDrive *const drive, const uint8_t phases,
                           const bool sel, const bool enabled)
{
    const bool prev_strobe = (drive->phases & 0x8) != 0 && drive->enabled;
    const bool cur_strobe = (phases & 0x8) != 0 && enabled;

    drive->phases = phases;
    drive->sel = sel;
    drive->enabled = enabled;

    if (!prev_strobe && cur_strobe) {
        sony_drive_strobe_drive(drive);
    }
}

bool sony_drive_read_byte(SonyDrive *const drive,
                          uint8_t *const value_out, bool *const is_mark_out)
{
    if (!drive->xfer_active || drive->xfer_position >= drive->xfer_length) {
        sony_drive_prepare(drive);
    }

    if (!drive->xfer_active) {
        return false;
    } else {
        const uint16_t offset = drive->xfer_position;
        drive->xfer_position++;
        const uint8_t value = drive->xfer_buffer[offset];
        const bool is_mark = bitset_get(drive->xfer_mark_bitset, offset);

        if (drive->gcr_encode) {
            assert(value & 0x80);
        }

        *value_out = value;
        *is_mark_out = is_mark;

        trace_sony_drive_read_byte(drive, value, is_mark,
                                   drive->xfer_lba, offset);
        return true;
    }
}

bool sony_drive_write_byte(SonyDrive *const drive, const uint8_t value)
{
    if (drive->write_protected) return false;

    /*
     * NOTE: for now, we should't get a write unless we have an active sector.
     * We'll have to revisit this to implement duplicating/formatting.
     */
    assert(drive->xfer_active);

    if (!drive->xfer_active) {
        return false;
    } else {
        const uint16_t offset = drive->xfer_write_position;
        assert(offset < drive->xfer_length);

        drive->xfer_buffer[offset] = value;
        drive->xfer_write_position++;
        drive->xfer_dirty = true;

        trace_sony_drive_write_byte(drive, value, drive->xfer_lba, offset);
        return true;
    }
}

void sony_drive_set_block_backend(SonyDrive *const drive,
                                  BlockBackend *const block)
{
    sony_drive_transfer_reset(drive);
    drive->blk = block;
    drive->disk_in = blk_is_inserted(block);
    sony_drive_reset(drive);
}

void sony_drive_reset(SonyDrive *const drive)
{
    sony_drive_transfer_reset(drive);

    drive->motor_on = false;
    drive->write_protected = false;
    drive->hd_media = false;
    drive->double_sided = false;
    drive->use_gcr = true;
    drive->gcr_encode = true;
    drive->cylinders = 0;
    drive->sectors_per_track = 0;
    drive->total_sectors = 0;
    drive->current_track = 0;
    drive->current_sector = 0;
    drive->seek_direction = 1;

    if (drive->disk_in) {
        const int64_t length = blk_getlength(drive->blk);

        if (length <= 0 || length % 512 != 0) {
            warn_report_once("%s: unsupported floppy size %" PRId64,
                             __func__, length);
            drive->disk_in = false;
        } else {
            drive->write_protected = !blk_is_writable(drive->blk);
            drive->total_sectors = length / 512;

            if (drive->total_sectors == 1600) {
                /* 800 kB (double-sided GCR). */
                drive->cylinders = 80;
                drive->double_sided = true;
                drive->gcr_encode = true;
            } else if (drive->total_sectors == 800) {
                /* 400 kB (single-sided GCR). */
                drive->cylinders = 80;
                drive->double_sided = false;
                drive->gcr_encode = true;
            } else if (drive->total_sectors == 2880) {
                /* 1440 kB (double-sided MFM). */
                drive->cylinders = 80;
                drive->sectors_per_track = 18;
                drive->hd_media = true;
                drive->double_sided = true;
                drive->gcr_encode = false;
            } else {
                warn_report_once("%s: unable to determine geometry for media"
                                 " (%" PRId64 " bytes)", __func__, length);
                drive->disk_in = false;
            }
        }
    }
}
