/*
 * QEMU Macintosh SuperDrive floppy disk drive emulator
 *
 * Copyright (c) 2025 Matt Jacobson <mhjacobson@me.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef HW_BLOCK_SWIM_SONY_H
#define HW_BLOCK_SWIM_SONY_H

#include <stdbool.h>
#include <stdint.h>
#include "hw/block/block.h"

typedef struct {
    BlockBackend *blk;

    uint8_t phases;
    bool sel;
    bool enabled;

    bool motor_on;
    bool disk_in;
    bool write_protected;
    bool hd_media;
    bool double_sided;
    bool use_gcr;
    bool gcr_encode;
    uint8_t cylinders;
    uint8_t sectors_per_track;
    uint8_t current_track;
    uint8_t current_sector;
    int8_t seek_direction;
    uint32_t total_sectors;

    bool xfer_active;
    bool xfer_dirty;
    uint64_t xfer_lba;
    uint16_t xfer_position;
    uint16_t xfer_write_position;
    uint16_t xfer_length;
    uint8_t xfer_buffer[1024];
    uint8_t xfer_mark_bitset[1024 / 8];
} SonyDrive;

void sony_drive_set_block_backend(SonyDrive *drive, BlockBackend *block);
void sony_drive_reset(SonyDrive *drive);
bool sony_drive_read_sense(SonyDrive *drive);
void sony_drive_set_inputs(SonyDrive *drive, uint8_t phases, bool sel, bool enabled);
bool sony_drive_read_byte(SonyDrive *drive, uint8_t *value_out, bool *is_mark_out);
bool sony_drive_write_byte(SonyDrive *const drive, const uint8_t value);

#endif /* HW_BLOCK_SWIM_SONY_H */
