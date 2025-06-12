/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU CXL Hotness Monitoring Unit
 *
 * Copyright (c) 2025 Huawei
 */

#include "hw/register.h"

#ifndef _CXL_CHMU_H_
#define _CXL_CHMU_H_

/* Emulated parameters - arbitrary choices */
#define CXL_CHMU_INSTANCES_PER_BLOCK 1
#define CXL_HOTLIST_ENTRIES 1024

/* 1TB - should be enough for anyone, right? */
#define CXL_MAX_DRAM_CAPACITY 0x10000000000UL

/* Relative to per instance base address */
#define CXL_CHMU_HL_START (0x70 + (CXL_MAX_DRAM_CAPACITY / (0x10000000UL * 8)))
#define CXL_CHMU_INSTANCE_SIZE (CXL_CHMU_HL_START + CXL_HOTLIST_ENTRIES * 8)
#define CXL_CHMU_SIZE \
    (0x10 + CXL_CHMU_INSTANCE_SIZE * CXL_CHMU_INSTANCES_PER_BLOCK)

/*
 * Many of these registers are documented as being a multiple of 64 bits long.
 * Reading then can only be done in 64 bit chunks though so specify them here
 * as multiple registers.
 */
REG64(CXL_CHMU_COMMON_CAP0, 0x0)
    FIELD(CXL_CHMU_COMMON_CAP0, VERSION, 0, 4)
    FIELD(CXL_CHMU_COMMON_CAP0, NUM_INSTANCES, 8, 8)
REG64(CXL_CHMU_COMMON_CAP1, 0x8)
    FIELD(CXL_CHMU_COMMON_CAP1, INSTANCE_LENGTH, 0, 16)

/* Per instance registers for instance 0 in CHMU main address space */
REG64(CXL_CHMU0_CAP0, 0x10)
    FIELD(CXL_CHMU0_CAP0, MSI_N, 0, 4)
    FIELD(CXL_CHMU0_CAP0, OVERFLOW_INT, 4, 1)
    FIELD(CXL_CHMU0_CAP0, LEVEL_INT, 5, 1)
    FIELD(CXL_CHMU0_CAP0, EPOCH_TYPE, 6, 2)
#define CXL_CHMU0_CAP0_EPOCH_TYPE_GLOBAL 0
#define CXL_CHMU0_CAP0_EPOCH_TYPE_PERCNT 1
    /* Break up the Tracked M2S Request field into flags */
    FIELD(CXL_CHMU0_CAP0, TRACKED_M2S_REQ_NONTEE_R, 8, 1)
    FIELD(CXL_CHMU0_CAP0, TRACKED_M2S_REQ_NONTEE_W, 9, 1)
    FIELD(CXL_CHMU0_CAP0, TRACKED_M2S_REQ_NONTEE_RW, 10, 1)
    FIELD(CXL_CHMU0_CAP0, TRACKED_M2S_REQ_ALL_R, 11, 1)
    FIELD(CXL_CHMU0_CAP0, TRACKED_M2S_REQ_ALL_W, 12, 1)
    FIELD(CXL_CHMU0_CAP0, TRACKED_M2S_REQ_ALL_RW, 13, 1)

    FIELD(CXL_CHMU0_CAP0, MAX_EPOCH_LENGTH_SCALE, 16, 4)
#define CXL_CHMU_EPOCH_LENGTH_SCALE_100USEC 1
#define CXL_CHMU_EPOCH_LENGTH_SCALE_1MSEC 2
#define CXL_CHMU_EPOCH_LENGTH_SCALE_10MSEC 3
#define CXL_CHMU_EPOCH_LENGTH_SCALE_100MSEC 4
#define CXL_CHMU_EPOCH_LENGTH_SCALE_1SEC 5
    FIELD(CXL_CHMU0_CAP0, MAX_EPOCH_LENGTH_VAL, 20, 12)
    FIELD(CXL_CHMU0_CAP0, MIN_EPOCH_LENGTH_SCALE, 32, 4)
    FIELD(CXL_CHMU0_CAP0, MIN_EPOCH_LENGTH_VAL, 36, 12)
    FIELD(CXL_CHMU0_CAP0, HOTLIST_SIZE, 48, 16)
REG64(CXL_CHMU0_CAP1, 0x18)
    FIELD(CXL_CHMU0_CAP1, UNIT_SIZES, 0, 32)
    FIELD(CXL_CHMU0_CAP1, DOWN_SAMPLING_FACTORS, 32, 16)
    /* Split up Flags */
    FIELD(CXL_CHMU0_CAP1, FLAGS_EPOCH_BASED, 48, 1)
    FIELD(CXL_CHMU0_CAP1, FLAGS_ALWAYS_ON, 49, 1)
    FIELD(CXL_CHMU0_CAP1, FLAGS_RANDOMIZED_DOWN_SAMPLING, 50, 1)
    FIELD(CXL_CHMU0_CAP1, FLAGS_OVERLAPPING_ADDRESS_RANGES, 51, 1)
    FIELD(CXL_CHMU0_CAP1, FLAGS_INSERT_AFTER_CLEAR, 52, 1)
REG64(CXL_CHMU0_CAP2, 0x20)
    FIELD(CXL_CHMU0_CAP2, BITMAP_REG_OFFSET, 0, 64)
REG64(CXL_CHMU0_CAP3, 0x28)
    FIELD(CXL_CHMU0_CAP3, HOTLIST_REG_OFFSET, 0, 64)

REG64(CXL_CHMU0_CONF0, 0x50)
    FIELD(CXL_CHMU0_CONF0, M2S_REQ_TO_TRACK, 0, 8)
    FIELD(CXL_CHMU0_CONF0, FLAGS_RANDOMIZE_DOWNSAMPLING, 8, 1)
    FIELD(CXL_CHMU0_CONF0, FLAGS_INT_ON_OVERFLOW, 9, 1)
    FIELD(CXL_CHMU0_CONF0, FLAGS_INT_ON_FILL_THRESH, 10, 1)
    FIELD(CXL_CHMU0_CONF0, CONTROL_ENABLE, 16, 1)
    FIELD(CXL_CHMU0_CONF0, CONTROL_RESET, 17, 1)
    FIELD(CXL_CHMU0_CONF0, HOTNESS_THRESHOLD, 32, 32)
REG64(CXL_CHMU0_CONF1, 0x58)
    FIELD(CXL_CHMU0_CONF1, UNIT_SIZE, 0, 32)
    FIELD(CXL_CHMU0_CONF1, DOWN_SAMPLING_FACTOR, 32, 8)
    FIELD(CXL_CHMU0_CONF1, REPORTING_MODE, 40, 8)
    FIELD(CXL_CHMU0_CONF1, EPOCH_LENGTH_SCALE, 48, 4)
    FIELD(CXL_CHMU0_CONF1, EPOCH_LENGTH_VAL, 52, 12)
REG64(CXL_CHMU0_CONF2, 0x60)
    FIELD(CXL_CHMU0_CONF2, NOTIFICATION_THRESHOLD, 0, 16)

REG64(CXL_CHMU0_STATUS, 0x70)
    /* Break up status field into separate flags */
    FIELD(CXL_CHMU0_STATUS, STATUS_ENABLED, 0, 1)
    FIELD(CXL_CHMU0_STATUS, OPERATION_IN_PROG, 16, 16)
    FIELD(CXL_CHMU0_STATUS, COUNTER_WIDTH, 32, 8)
    /* Break up oddly named overflow interrupt stats */
    FIELD(CXL_CHMU0_STATUS, OVERFLOW_INT, 40, 1)
    FIELD(CXL_CHMU0_STATUS, LEVEL_INT, 41, 1)

REG16(CXL_CHMU0_HEAD, 0x78)
REG16(CXL_CHMU0_TAIL, 0x7A)

/* Provide first few of these so we can calculate the size */
REG64(CXL_CHMU0_RANGE_CONFIG_BITMAP0, 0x80)
REG64(CXL_CHMU0_RANGE_CONFIG_BITMAP1, 0x88)

REG64(CXL_CHMU0_HOTLIST0, CXL_CHMU_HL_START + 0x10)
REG64(CXL_CHMU0_HOTLIST1, CXL_CHMU_HL_START + 0x10)

REG64(CXL_CHMU1_CAP0, 0x10 + CXL_CHMU_INSTANCE_SIZE)

typedef struct CHMUState CHMUState;

/*
 * Each device may have multiple CHMUs (CHMUState) with each CHMU having
 * multiple hotness tracker instances (CHMUInstance).
 */
typedef struct CHMUInstance {
    /* The reference to the PCIDevice is needed for MSI */
    Object *private;
    /* Number of counts in an epoch to be considered hot */
    uint32_t hotness_thresh;
    /* Tracking unit in bytes of DPA space as power of 2 */
    uint32_t unit_size;
    /*
     * Ring buffer pointers
     * - head is the offset in the ring of the oldest hot unit
     * - tail is the offset in the ring of where the next hot unit will be
     *   saved.
     *
     * Ring empty if head == tail.
     * Ring full if (tail + 1) % length == head
     */
    uint16_t head, tail;
    /* Ring buffer event threshold. Interrupt of first exceeding */
    uint16_t fill_thresh;
    /* Down sampling factor */
    uint8_t ds_factor;
    /* Type of request to track */
    uint8_t what;

    /* Interrupt controls and status */
    bool int_on_overflow;
    bool int_on_fill_thresh;
    bool overflow_set;
    bool fill_thresh_set;
    uint8_t msi_n;

    bool enabled;
    uint64_t hotlist[CXL_HOTLIST_ENTRIES];
    QEMUTimer *timer;
    uint32_t epoch_ms;
    uint8_t epoch_scale;
    uint16_t epoch_val;
    /* Reference needed for timer */
    CHMUState *parent;
} CHMUInstance;

typedef struct CHMUState {
    CHMUInstance inst[CXL_CHMU_INSTANCES_PER_BLOCK];
    int socket;
    /* Hack updated on first HDM decoder only */
    uint16_t port;

    /*
     * Routing of accesses depends on interleave settings of the
     * relevant memory range. That must be passed to the cache plugin.
     */
    struct {
        uint64_t base;
        uint64_t size;
        uint64_t dpa_base;
        uint16_t interleave_gran;
        uint8_t ways;
        uint8_t way;
    } decoder[CXL_HDM_DECODER_COUNT];
} CHMUState;

typedef struct cxl_device_state CXLDeviceState;
int cxl_chmu_register_block_init(Object *obj, CXLDeviceState *cxl_dstte,
                                 int id, uint8_t msi_n, Error **errp);

#endif /* _CXL_CHMU_H_ */
