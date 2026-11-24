/*
 * RISC-V Capacity and Bandwidth QoS Register Interface
 * URL: https://github.com/riscv-non-isa/riscv-cbqri/releases/tag/v1.0
 *
 * Copyright (c) 2023 BayLibre SAS
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file contains the Bandwidth-controller QoS Register Interface.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/cbqri.h"

/* Property defaults, shared with riscv_cbqri_bc_caps_init() */
#define BC_DEFAULT_MAX_MCIDS    256
#define BC_DEFAULT_MAX_RCIDS    64
#define BC_DEFAULT_NBWBLKS      1024
#define BC_DEFAULT_MRBWB        819

/* Encodings of `AT` field */
enum {
    BC_AT_DATA = 0,
    BC_AT_CODE = 1,
    BC_NB_ATS,
};

/* Capabilities */
REG64(BC_CAPABILITIES, 0);
FIELD(BC_CAPABILITIES, VER, 0, 8);
FIELD(BC_CAPABILITIES, VER_MINOR, 0, 4);
FIELD(BC_CAPABILITIES, VER_MAJOR, 4, 4);
FIELD(BC_CAPABILITIES, NBWBLKS, 8, 16);
FIELD(BC_CAPABILITIES, RPFX, 24, 1);
FIELD(BC_CAPABILITIES, P, 25, 4);
FIELD(BC_CAPABILITIES, MRBWB, 32, 16);

/* Usage monitoring control */
REG64(BC_MON_CTL, 8);
FIELD(BC_MON_CTL, OP, 0, 5);
FIELD(BC_MON_CTL, AT, 5, 3);
FIELD(BC_MON_CTL, MCID, 8, 12);
FIELD(BC_MON_CTL, EVT_ID, 20, 8);
FIELD(BC_MON_CTL, ATV, 28, 1);
FIELD(BC_MON_CTL, STATUS, 32, 7);
FIELD(BC_MON_CTL, BUSY, 39, 1);

/* Usage monitoring operations */
enum {
    BC_MON_OP_CONFIG_EVENT = 1,
    BC_MON_OP_READ_COUNTER = 2,
};

/* Bandwidth monitoring event ID */
enum {
    BC_EVT_ID_None = 0,
    BC_EVT_ID_RDWR_count = 1,
    BC_EVT_ID_RDONLY_count = 2,
    BC_EVT_ID_WRONLY_count = 3,
};

/* BC_MON_CTL.STATUS field encodings */
enum {
    BC_MON_CTL_STATUS_SUCCESS = 1,
    BC_MON_CTL_STATUS_INVAL_OP = 2,
    BC_MON_CTL_STATUS_INVAL_MCID = 3,
    BC_MON_CTL_STATUS_INVAL_EVT_ID = 4,
    BC_MON_CTL_STATUS_INVAL_AT = 5,
};

/* Monitoring counter value */
REG64(BC_MON_CTR_VAL, 16);
FIELD(BC_MON_CTR_VAL, CTR, 0, 62);
FIELD(BC_MON_CTR_VAL, INVALID, 62, 1);
FIELD(BC_MON_CTR_VAL, OVF, 63, 1);

/* Bandwidth Allocation control */
REG64(BC_ALLOC_CTL, 24);
FIELD(BC_ALLOC_CTL, OP, 0, 5);
FIELD(BC_ALLOC_CTL, AT, 5, 3);
FIELD(BC_ALLOC_CTL, RCID, 8, 12);
FIELD(BC_ALLOC_CTL, STATUS, 32, 7);
FIELD(BC_ALLOC_CTL, BUSY, 39, 1);

/* Bandwidth allocation operations */
enum {
    BC_ALLOC_OP_CONFIG_LIMIT = 1,
    BC_ALLOC_OP_READ_LIMIT = 2,
};

/* BC_ALLOC_CTL.STATUS field encodings */
enum {
    BC_ALLOC_STATUS_SUCCESS = 1,
    BC_ALLOC_STATUS_INVAL_OP = 2,
    BC_ALLOC_STATUS_INVAL_RCID = 3,
    BC_ALLOC_STATUS_INVAL_AT = 4,
    BC_ALLOC_STATUS_INVAL_BLKS = 5,
};

/* Bandwidth allocation */
REG64(BC_BW_ALLOC, 32);
FIELD(BC_BW_ALLOC, Rbwb, 0, 16);
FIELD(BC_BW_ALLOC, Mweight, 20, 8);
FIELD(BC_BW_ALLOC, sharedAT, 28, 3);
FIELD(BC_BW_ALLOC, useShared, 31, 1);


typedef struct RiscvCbqriBandwidthAllocation {
    uint32_t Rbwb:16;
    uint32_t Mweight:8;
    uint32_t sharedAT:3;
    bool useShared:1;
} RiscvCbqriBandwidthAllocation;

typedef struct RiscvCbqriBandwidthState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* cached value of some registers */
    uint64_t bc_mon_ctl;
    uint64_t bc_mon_ctr_val;
    uint64_t bc_alloc_ctl;

    RiscvCbqriMonitorCounter *mon_counters;
    RiscvCbqriBandwidthAllocation *bw_allocations;

    /* properties */

    uint64_t mmio_base;
    uint16_t nb_mcids;
    uint16_t nb_rcids;

    uint16_t nbwblks;
    uint16_t mrbwb;

    bool supports_at_code;

    bool supports_alloc_op_config_limit;
    bool supports_alloc_op_read_limit;

    bool supports_mon_op_config_event;
    bool supports_mon_op_read_counter;

    bool supports_mon_evt_id_none;
    bool supports_mon_evt_id_rdwr_count;
    bool supports_mon_evt_id_rdonly_count;
    bool supports_mon_evt_id_wronly_count;
} RiscvCbqriBandwidthState;

#define RISCV_CBQRI_BC(obj) \
    OBJECT_CHECK(RiscvCbqriBandwidthState, (obj), TYPE_RISCV_CBQRI_BC)

/*
 * All bandwidth allocation records are contiguous to simplify
 * allocation. The first one is used to hold the BC_BW_ALLOC register
 * content, followed by respective records for each AT per RCID.
 *
 * Records are indexed by the AT encoding, so each RCID has one record
 * per AT value whether or not the controller supports the code access
 * type. is_valid_at() rejects an unsupported AT before a record is
 * selected.
 */
static unsigned int bw_alloc_index(uint32_t rcid, uint32_t at)
{
    g_assert(at < BC_NB_ATS);

    return 1 + rcid * BC_NB_ATS + at;
}

static RiscvCbqriBandwidthAllocation *get_bw_alloc(RiscvCbqriBandwidthState *bc,
                                                   uint32_t rcid, uint32_t at)
{
    return &bc->bw_allocations[bw_alloc_index(rcid, at)];
}

static uint32_t bandwidth_config(RiscvCbqriBandwidthState *bc,
                                 uint32_t rcid, uint32_t at,
                                 bool *busy)
{
    RiscvCbqriBandwidthAllocation *bw_alloc = get_bw_alloc(bc, rcid, at);
    RiscvCbqriBandwidthAllocation *end = get_bw_alloc(bc, bc->nb_rcids, 0);
    RiscvCbqriBandwidthAllocation *cur;
    uint32_t rbwb = bc->bw_allocations[0].Rbwb;
    uint64_t sum = rbwb;

    /*
     * When useShared is set the allocation of sharedAT applies instead
     * and the Rbwb and Mweight fields are ignored.
     */
    if (bc->bw_allocations[0].useShared) {
        *bw_alloc = bc->bw_allocations[0];
        return BC_ALLOC_STATUS_SUCCESS;
    }

    /*
     * Bandwidth is allocated in multiples of bandwidth blocks, and the
     * value in Rbwb must be at least 1 and must not exceed the MRBWB value.
     */
    if (rbwb < 1 || rbwb > bc->mrbwb) {
        return BC_ALLOC_STATUS_INVAL_BLKS;
    }

    /*
     * The sum of Rbwb allocated across all RCIDs must not exceed MRBWB
     * either. The allocation being replaced and allocations shared with
     * another access type do not count.
     */
    for (cur = &bc->bw_allocations[1]; cur < end; cur++) {
        if (cur != bw_alloc && !cur->useShared) {
            sum += cur->Rbwb;
        }
    }
    if (sum > bc->mrbwb) {
        return BC_ALLOC_STATUS_INVAL_BLKS;
    }

    /* Save contents of BC_BW_ALLOC register for this rcid and at */
    *bw_alloc = bc->bw_allocations[0];
    return BC_ALLOC_STATUS_SUCCESS;
}

static uint32_t bandwidth_read(RiscvCbqriBandwidthState *bc,
                               uint32_t rcid, uint32_t at,
                               bool *busy)
{
    RiscvCbqriBandwidthAllocation *bw_alloc = get_bw_alloc(bc, rcid, at);

    /* Populate BC_BW_ALLOC register with selected content */
    bc->bw_allocations[0] = *bw_alloc;
    return BC_ALLOC_STATUS_SUCCESS;
}

static bool is_valid_at(RiscvCbqriBandwidthState *bc, uint32_t at)
{
    switch (at) {
    case BC_AT_DATA:
        return true;
    case BC_AT_CODE:
        return bc->supports_at_code;
    default:
        return false;
    }
}

static void riscv_cbqri_bc_write_mon_ctl(RiscvCbqriBandwidthState *bc,
                                         uint64_t value)
{
    if (!bc->supports_mon_op_config_event &&
        !bc->supports_mon_op_read_counter) {
        /* monitoring not supported: leave mon_ctl set to 0 */
        return;
    }

    /* extract writable fields */
    uint32_t op = FIELD_EX64(value, BC_MON_CTL, OP);
    uint32_t at = FIELD_EX64(value, BC_MON_CTL, AT);
    uint32_t mcid = FIELD_EX64(value, BC_MON_CTL, MCID);
    uint32_t evt_id = FIELD_EX64(value, BC_MON_CTL, EVT_ID);
    bool atv = FIELD_EX64(value, BC_MON_CTL, ATV);

    /* extract read-only fields */
    uint32_t status = FIELD_EX64(bc->bc_mon_ctl, BC_MON_CTL, STATUS);
    bool busy = FIELD_EX64(bc->bc_mon_ctl, BC_MON_CTL, BUSY);

    if (busy) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: busy flag still set, ignored\n",
                      __func__);
        return;
    }

    if (!bc->supports_at_code) {
        /* AT not supported: hardwire to 0 */
        at = 0;
        atv = false;
    }

    if (mcid >= bc->nb_mcids) {
        status = BC_MON_CTL_STATUS_INVAL_MCID;
    } else if (op == BC_MON_OP_CONFIG_EVENT &&
               bc->supports_mon_op_config_event) {
        if (evt_id == BC_EVT_ID_None &&
            bc->supports_mon_evt_id_none) {
            bc->mon_counters[mcid].active = false;
            status = BC_MON_CTL_STATUS_SUCCESS;
        } else if ((evt_id == BC_EVT_ID_RDWR_count &&
                    bc->supports_mon_evt_id_rdwr_count) ||
                   (evt_id == BC_EVT_ID_RDONLY_count &&
                    bc->supports_mon_evt_id_rdonly_count) ||
                   (evt_id == BC_EVT_ID_WRONLY_count &&
                    bc->supports_mon_evt_id_wronly_count)) {
            if (atv && !is_valid_at(bc, at)) {
                status = BC_MON_CTL_STATUS_INVAL_AT;
            } else {
                /*
                 * The model does not account for bandwidth usage, so the
                 * counter is marked INVALID and stays that way.
                 */
                bc->mon_counters[mcid].ctr_val =
                    FIELD_DP64(0, BC_MON_CTR_VAL, INVALID, 1);
                bc->mon_counters[mcid].evt_id = evt_id;
                bc->mon_counters[mcid].at = atv ? at : -1;
                bc->mon_counters[mcid].active = true;
                status = BC_MON_CTL_STATUS_SUCCESS;
            }
        } else {
            status = BC_MON_CTL_STATUS_INVAL_EVT_ID;
        }
    } else if (op == BC_MON_OP_READ_COUNTER &&
               bc->supports_mon_op_read_counter) {
        bc->bc_mon_ctr_val = bc->mon_counters[mcid].ctr_val;
        status = BC_MON_CTL_STATUS_SUCCESS;
    } else {
        status = BC_MON_CTL_STATUS_INVAL_OP;
    }

    /* reconstruct updated register value */
    value = 0;
    value = FIELD_DP64(value, BC_MON_CTL, OP, op);
    value = FIELD_DP64(value, BC_MON_CTL, AT, at);
    value = FIELD_DP64(value, BC_MON_CTL, MCID, mcid);
    value = FIELD_DP64(value, BC_MON_CTL, EVT_ID, evt_id);
    value = FIELD_DP64(value, BC_MON_CTL, ATV, atv);
    value = FIELD_DP64(value, BC_MON_CTL, STATUS, status);
    value = FIELD_DP64(value, BC_MON_CTL, BUSY, busy);
    bc->bc_mon_ctl = value;
}

static void riscv_cbqri_bc_write_alloc_ctl(RiscvCbqriBandwidthState *bc,
                                           uint64_t value)
{
    if (bc->nbwblks == 0 ||
        (!bc->supports_alloc_op_config_limit &&
         !bc->supports_alloc_op_read_limit)) {
        /* bandwidth allocation not supported: leave bc_alloc_ctl set to 0 */
        return;
    }

    /* extract writable fields */
    uint32_t op = FIELD_EX64(value, BC_ALLOC_CTL, OP);
    uint32_t at = FIELD_EX64(value, BC_ALLOC_CTL, AT);
    uint32_t rcid = FIELD_EX64(value, BC_ALLOC_CTL, RCID);

    /* extract read-only fields */
    uint32_t status = FIELD_EX64(bc->bc_alloc_ctl, BC_ALLOC_CTL, STATUS);
    bool busy = FIELD_EX64(bc->bc_alloc_ctl, BC_ALLOC_CTL, BUSY);

    if (busy) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: busy flag still set, ignored\n",
                      __func__);
        return;
    }

    bool atv = true;
    if (!bc->supports_at_code) {
        /* AT not supported: hardwire to 0 */
        at = 0;
        atv = false;
    }

    if (rcid >= bc->nb_rcids) {
        status = BC_ALLOC_STATUS_INVAL_RCID;
    } else if (atv && !is_valid_at(bc, at)) {
        status = BC_ALLOC_STATUS_INVAL_AT;
    } else if (op == BC_ALLOC_OP_CONFIG_LIMIT &&
               bc->supports_alloc_op_config_limit) {
        status = bandwidth_config(bc, rcid, at, &busy);
    } else if (op == BC_ALLOC_OP_READ_LIMIT &&
               bc->supports_alloc_op_read_limit) {
        status = bandwidth_read(bc, rcid, at, &busy);
    } else {
        status = BC_ALLOC_STATUS_INVAL_OP;
    }

    /* reconstruct updated register value */
    value = 0;
    value = FIELD_DP64(value, BC_ALLOC_CTL, OP, op);
    value = FIELD_DP64(value, BC_ALLOC_CTL, AT, at);
    value = FIELD_DP64(value, BC_ALLOC_CTL, RCID, rcid);
    value = FIELD_DP64(value, BC_ALLOC_CTL, STATUS, status);
    value = FIELD_DP64(value, BC_ALLOC_CTL, BUSY, busy);
    bc->bc_alloc_ctl = value;
}

static void riscv_cbqri_bc_write_bw_alloc(RiscvCbqriBandwidthState *bc,
                                          uint64_t value)
{
    if (bc->nbwblks == 0) {
        /* bandwidth allocation not supported: leave bw_alloc set to 0 */
        return;
    }

    RiscvCbqriBandwidthAllocation *bc_bw_alloc = &bc->bw_allocations[0];

    /* extract writable fields */
    bc_bw_alloc->Rbwb = FIELD_EX64(value, BC_BW_ALLOC, Rbwb);
    bc_bw_alloc->Mweight = FIELD_EX64(value, BC_BW_ALLOC, Mweight);
    bc_bw_alloc->sharedAT = FIELD_EX64(value, BC_BW_ALLOC, sharedAT);
    bc_bw_alloc->useShared = FIELD_EX64(value, BC_BW_ALLOC, useShared);

    if (!bc->supports_at_code) {
        /* AT not supported: hardwire to 0 */
        bc_bw_alloc->sharedAT = 0;
        bc_bw_alloc->useShared = false;
    } else if (!is_valid_at(bc, bc_bw_alloc->sharedAT)) {
        /* sharedAT is WARL: an unsupported AT reads back as AT 0 */
        bc_bw_alloc->sharedAT = BC_AT_DATA;
    }
}

static void riscv_cbqri_bc_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    RiscvCbqriBandwidthState *bc = opaque;

    g_assert((addr % 8) == 0);

    switch (addr) {
    case A_BC_CAPABILITIES:
        /* read-only register */
        break;
    case A_BC_MON_CTL:
        riscv_cbqri_bc_write_mon_ctl(bc, value);
        break;
    case A_BC_MON_CTR_VAL:
        /* read-only register */
        break;
    case A_BC_ALLOC_CTL:
        riscv_cbqri_bc_write_alloc_ctl(bc, value);
        break;
    case A_BC_BW_ALLOC:
        riscv_cbqri_bc_write_bw_alloc(bc, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out of bounds (addr=0x%x)\n",
                      __func__, (uint32_t)addr);
    }
}

static uint64_t riscv_cbqri_bc_read(void *opaque, hwaddr addr, unsigned size)
{
    RiscvCbqriBandwidthState *bc = opaque;
    uint64_t value = 0;

    g_assert((addr % 8) == 0);

    switch (addr) {
    case A_BC_CAPABILITIES:
        value = FIELD_DP64(value, BC_CAPABILITIES, VER_MAJOR,
                           RISCV_CBQRI_VERSION_MAJOR);
        value = FIELD_DP64(value, BC_CAPABILITIES, VER_MINOR,
                           RISCV_CBQRI_VERSION_MINOR);
        value = FIELD_DP64(value, BC_CAPABILITIES, RPFX, 0);
        value = FIELD_DP64(value, BC_CAPABILITIES, P, 0);
        value = FIELD_DP64(value, BC_CAPABILITIES, NBWBLKS, bc->nbwblks);
        value = FIELD_DP64(value, BC_CAPABILITIES, MRBWB, bc->mrbwb);
        break;
    case A_BC_MON_CTL:
        value = bc->bc_mon_ctl;
        break;
    case A_BC_MON_CTR_VAL:
        value = bc->bc_mon_ctr_val;
        break;
    case A_BC_ALLOC_CTL:
        value = bc->bc_alloc_ctl;
        break;
    case A_BC_BW_ALLOC:
        RiscvCbqriBandwidthAllocation *bc_bw_alloc = &bc->bw_allocations[0];
        value = FIELD_DP64(value, BC_BW_ALLOC, Rbwb, bc_bw_alloc->Rbwb);
        value = FIELD_DP64(value, BC_BW_ALLOC, Mweight, bc_bw_alloc->Mweight);
        value = FIELD_DP64(value, BC_BW_ALLOC, sharedAT, bc_bw_alloc->sharedAT);
        value = FIELD_DP64(value, BC_BW_ALLOC, useShared,
                           bc_bw_alloc->useShared);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out of bounds (addr=0x%x)\n",
                      __func__, (uint32_t)addr);
    }

    return value;
}

static uint64_t riscv_cbqri_bc_read_wrapper(void *opaque, hwaddr addr,
                                            unsigned size)
{
    uint64_t value = riscv_cbqri_bc_read(opaque, QEMU_ALIGN_DOWN(addr, 8), 8);
    if (size == 4) {
        if (addr & 0x7) {
            return value >> 32;
        } else {
            return value & MAKE_64BIT_MASK(0, 32);
        }
    }
    return value;
}

static void riscv_cbqri_bc_write_wrapper(void *opaque, hwaddr addr,
                                             uint64_t value, unsigned size)
{
    hwaddr reg_addr = QEMU_ALIGN_DOWN(addr, 8);

    if (size == 4) {
        uint64_t reg = riscv_cbqri_bc_read(opaque, reg_addr, 8);
        if (addr & 0x7) {
            /*
             * The upper half of the control registers holds only the
             * read-only STATUS and BUSY fields. Writing it must not run
             * the operation held in the lower half again.
             */
            if (reg_addr == A_BC_MON_CTL || reg_addr == A_BC_ALLOC_CTL) {
                return;
            }
            value = value << 32 | (reg & MAKE_64BIT_MASK(0, 32));
        } else {
            value = value | (reg & MAKE_64BIT_MASK(32, 32));
        }
    }
    riscv_cbqri_bc_write(opaque, reg_addr, value, 8);
}


static const MemoryRegionOps riscv_cbqri_bc_ops = {
    .read = riscv_cbqri_bc_read_wrapper,
    .write = riscv_cbqri_bc_write_wrapper,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
};

static void riscv_cbqri_bc_realize(DeviceState *dev, Error **errp)
{
    RiscvCbqriBandwidthState *bc = RISCV_CBQRI_BC(dev);

    if (!bc->mmio_base) {
        error_setg(errp, "mmio_base property not set");
        return;
    }

    if (bc->nb_rcids == 0) {
        error_setg(errp, "max_rcids must be at least 1");
        return;
    }

    if (bc->mrbwb > bc->nbwblks) {
        error_setg(errp, "mrbwb must not exceed nbwblks");
        return;
    }

    if (bc->nbwblks != 0 && bc->mrbwb == 0) {
        error_setg(errp, "mrbwb must be at least 1 when nbwblks is not 0");
        return;
    }

    g_assert(bc->mon_counters == NULL);
    bc->mon_counters = g_new0(RiscvCbqriMonitorCounter, bc->nb_mcids);

    g_assert(bc->bw_allocations == NULL);
    bc->bw_allocations = g_new0(RiscvCbqriBandwidthAllocation,
                                bw_alloc_index(bc->nb_rcids, 0));

    memory_region_init_io(&bc->mmio, OBJECT(dev), &riscv_cbqri_bc_ops,
                          bc, TYPE_RISCV_CBQRI_BC".mmio", 4 * 1024);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &bc->mmio);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, bc->mmio_base);
}

static void riscv_cbqri_bc_reset_hold(Object *obj, ResetType type)
{
    RiscvCbqriBandwidthState *bc = RISCV_CBQRI_BC(obj);
    RiscvCbqriBandwidthAllocation *bw_alloc;
    unsigned int i;

    /*
     * The spec requires that the reset value is 0 for the bc_mon_ctl.BUSY
     * and bc_alloc_ctl.BUSY fields, and that the reset value is UNSPECIFIED
     * for all other register fields, so every allocation and counter is
     * cleared and a rebooted guest starts from a known state.
     */
    bc->bc_mon_ctl = 0;
    bc->bc_alloc_ctl = 0;
    bc->bc_mon_ctr_val = 0;
    for (i = 0; i < bc->nb_mcids; i++) {
        bc->mon_counters[i] = (RiscvCbqriMonitorCounter) {
            .ctr_val = FIELD_DP64(0, BC_MON_CTR_VAL, INVALID, 1),
        };
    }
    memset(bc->bw_allocations, 0,
           bw_alloc_index(bc->nb_rcids, 0) * sizeof(*bc->bw_allocations));

    /*
     * The bandwidth controllers at reset must allocate all available
     * bandwidth to RCID value of 0. All available bandwidth is MRBWB,
     * the most that can be reserved.
     *
     * When the bandwidth controller supports bandwidth allocation per
     * access type, AT 0 of RCID=0 is allocated all available bandwidth
     * and the other access types share the allocation with AT 0.
     *
     * A controller with NBWBLKS of 0 does not support bandwidth
     * allocation and has nothing to allocate.
     */
    if (bc->nbwblks == 0) {
        return;
    }
    bw_alloc = get_bw_alloc(bc, 0, BC_AT_DATA);
    *bw_alloc = (RiscvCbqriBandwidthAllocation) {
        .Rbwb = bc->mrbwb,
    };
    if (bc->supports_at_code) {
        bw_alloc = get_bw_alloc(bc, 0, BC_AT_CODE);
        *bw_alloc = (RiscvCbqriBandwidthAllocation) {
            .sharedAT = BC_AT_DATA,
            .useShared = true,
        };
    }
}

static const Property riscv_cbqri_bc_properties[] = {
    DEFINE_PROP_UINT64("mmio_base", RiscvCbqriBandwidthState, mmio_base, 0),

    DEFINE_PROP_UINT16("max_mcids", RiscvCbqriBandwidthState, nb_mcids,
                       BC_DEFAULT_MAX_MCIDS),
    DEFINE_PROP_UINT16("max_rcids", RiscvCbqriBandwidthState, nb_rcids,
                       BC_DEFAULT_MAX_RCIDS),
    DEFINE_PROP_UINT16("nbwblks", RiscvCbqriBandwidthState, nbwblks,
                       BC_DEFAULT_NBWBLKS),
    DEFINE_PROP_UINT16("mrbwb", RiscvCbqriBandwidthState, mrbwb,
                       BC_DEFAULT_MRBWB),

    DEFINE_PROP_BOOL("at_code", RiscvCbqriBandwidthState,
                     supports_at_code, true),

    DEFINE_PROP_BOOL("alloc_op_config_limit", RiscvCbqriBandwidthState,
                     supports_alloc_op_config_limit, true),
    DEFINE_PROP_BOOL("alloc_op_read_limit", RiscvCbqriBandwidthState,
                     supports_alloc_op_read_limit, true),

    DEFINE_PROP_BOOL("mon_op_config_event", RiscvCbqriBandwidthState,
                     supports_mon_op_config_event, true),
    DEFINE_PROP_BOOL("mon_op_read_counter", RiscvCbqriBandwidthState,
                     supports_mon_op_read_counter, true),

    DEFINE_PROP_BOOL("mon_evt_id_none", RiscvCbqriBandwidthState,
                     supports_mon_evt_id_none, true),
    DEFINE_PROP_BOOL("mon_evt_id_rdwr_count", RiscvCbqriBandwidthState,
                     supports_mon_evt_id_rdwr_count, true),
    DEFINE_PROP_BOOL("mon_evt_id_rdonly_count", RiscvCbqriBandwidthState,
                     supports_mon_evt_id_rdonly_count, true),
    DEFINE_PROP_BOOL("mon_evt_id_wronly_count", RiscvCbqriBandwidthState,
                     supports_mon_evt_id_wronly_count, true),
};

static const VMStateDescription vmstate_riscv_cbqri_bc = {
    .name = "riscv-cbqri-bandwidth",
    .unmigratable = 1,
};

static void riscv_cbqri_bc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = riscv_cbqri_bc_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "RISC-V CBQRI Bandwidth Controller";
    device_class_set_props(dc, riscv_cbqri_bc_properties);
    rc->phases.hold = riscv_cbqri_bc_reset_hold;
    dc->vmsd = &vmstate_riscv_cbqri_bc;
    dc->user_creatable = true;
}

static const TypeInfo riscv_cbqri_bc_info = {
    .name          = TYPE_RISCV_CBQRI_BC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RiscvCbqriBandwidthState),
    .class_init    = riscv_cbqri_bc_class_init,
};

static void riscv_cbqri_bc_register_types(void)
{
    type_register_static(&riscv_cbqri_bc_info);
}

void riscv_cbqri_bc_caps_init(RiscvCbqriBandwidthCaps *caps)
{
    *caps = (RiscvCbqriBandwidthCaps) {
        .nb_mcids = BC_DEFAULT_MAX_MCIDS,
        .nb_rcids = BC_DEFAULT_MAX_RCIDS,
        .nbwblks = BC_DEFAULT_NBWBLKS,
        .mrbwb = BC_DEFAULT_MRBWB,
        .supports_at_code = true,
        .supports_alloc_op_config_limit = true,
        .supports_alloc_op_read_limit = true,
        .supports_mon_op_config_event = true,
        .supports_mon_op_read_counter = true,
        .supports_mon_evt_id_none = true,
        .supports_mon_evt_id_rdwr_count = true,
        .supports_mon_evt_id_rdonly_count = true,
        .supports_mon_evt_id_wronly_count = true,
    };
}

DeviceState *riscv_cbqri_bc_create(hwaddr addr,
                                   const RiscvCbqriBandwidthCaps *caps)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_CBQRI_BC);

    qdev_prop_set_uint64(dev, "mmio_base", addr);
    qdev_prop_set_uint16(dev, "max_mcids", caps->nb_mcids);
    qdev_prop_set_uint16(dev, "max_rcids", caps->nb_rcids);
    qdev_prop_set_uint16(dev, "nbwblks", caps->nbwblks);
    qdev_prop_set_uint16(dev, "mrbwb", caps->mrbwb);

    qdev_prop_set_bit(dev, "at_code",
                      caps->supports_at_code);
    qdev_prop_set_bit(dev, "alloc_op_config_limit",
                      caps->supports_alloc_op_config_limit);
    qdev_prop_set_bit(dev, "alloc_op_read_limit",
                      caps->supports_alloc_op_read_limit);
    qdev_prop_set_bit(dev, "mon_op_config_event",
                      caps->supports_mon_op_config_event);
    qdev_prop_set_bit(dev, "mon_op_read_counter",
                      caps->supports_mon_op_read_counter);
    qdev_prop_set_bit(dev, "mon_evt_id_none",
                      caps->supports_mon_evt_id_none);
    qdev_prop_set_bit(dev, "mon_evt_id_rdwr_count",
                      caps->supports_mon_evt_id_rdwr_count);
    qdev_prop_set_bit(dev, "mon_evt_id_rdonly_count",
                      caps->supports_mon_evt_id_rdonly_count);
    qdev_prop_set_bit(dev, "mon_evt_id_wronly_count",
                      caps->supports_mon_evt_id_wronly_count);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return dev;
}

type_init(riscv_cbqri_bc_register_types)
