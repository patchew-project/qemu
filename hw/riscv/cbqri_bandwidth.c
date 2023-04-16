/*
 * RISC-V Capacity and Bandwidth QoS Register Interface
 * URL: https://github.com/riscv-non-isa/riscv-cbqri
 *
 * Copyright (c) 2023 BayLibre SAS
 *
 * This file contains the Bandwidth-controller QoS Register Interface.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/cbqri.h"


/* Encodings of `AT` field */
enum {
    BC_AT_DATA = 0,
    BC_AT_CODE = 1,
};

/* Capabilities */
REG64(BC_CAPABILITIES, 0);
FIELD(BC_CAPABILITIES, VER, 0, 8);
FIELD(BC_CAPABILITIES, VER_MINOR, 0, 4);
FIELD(BC_CAPABILITIES, VER_MAJOR, 4, 4);
FIELD(BC_CAPABILITIES, NBWBLKS, 8, 16);
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


typedef struct MonitorCounter {
    uint64_t ctr_val;
    int at;
    int evt_id;
    bool active;
} MonitorCounter;

typedef struct BandwidthAllocation {
    uint32_t Rbwb:16;
    uint32_t Mweight:8;
    uint32_t sharedAT:3;
    bool useShared:1;
} BandwidthAllocation;

typedef struct RiscvCbqriBandwidthState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    const char *name;
    const RiscvCbqriBandwidthCaps *caps;

    /* cached value of some registers */
    uint64_t bc_mon_ctl;
    uint64_t bc_mon_ctr_val;
    uint64_t bc_alloc_ctl;
    uint64_t bc_bw_alloc;

    MonitorCounter *mon_counters;
    BandwidthAllocation *bw_allocations;
} RiscvCbqriBandwidthState;

#define TYPE_RISCV_CBQRI_BC "riscv.cbqri.bandwidth"

#define RISCV_CBQRI_BC(obj) \
    OBJECT_CHECK(RiscvCbqriBandwidthState, (obj), TYPE_RISCV_CBQRI_BC)

static BandwidthAllocation *get_bw_alloc(RiscvCbqriBandwidthState *bc,
                                      uint32_t rcid, uint32_t at)
{
    /*
     * All bandwidth allocation records are contiguous to simplify
     * allocation. The first one is used to hold the BC_BW_ALLOC register
     * content, followed by respective records for each AT per RCID.
     */

    unsigned int nb_ats = 0;
    nb_ats += !!bc->caps->supports_at_data;
    nb_ats += !!bc->caps->supports_at_code;
    nb_ats = MAX(nb_ats, 1);
    assert(at < nb_ats);

    return &bc->bw_allocations[1 + rcid * nb_ats + at];
}

static uint32_t bandwidth_config(RiscvCbqriBandwidthState *bc,
                                 uint32_t rcid, uint32_t at,
                                 bool *busy)
{
    BandwidthAllocation *bw_alloc = get_bw_alloc(bc, rcid, at);

    /* for now we only preserve the current BC_BW_ALLOC register content */
    *bw_alloc = bc->bw_allocations[0];
    return BC_ALLOC_STATUS_SUCCESS;
}

static uint32_t bandwidth_read(RiscvCbqriBandwidthState *bc,
                               uint32_t rcid, uint32_t at,
                               bool *busy)
{
    BandwidthAllocation *bw_alloc = get_bw_alloc(bc, rcid, at);

    /* populate BC_BW_ALLOC register with selected content */
    bc->bw_allocations[0] = *bw_alloc;
    return BC_ALLOC_STATUS_SUCCESS;
}

static bool is_valid_at(RiscvCbqriBandwidthState *bc, uint32_t at)
{
    switch (at) {
    case BC_AT_DATA:
        return bc->caps->supports_at_data;
    case BC_AT_CODE:
        return bc->caps->supports_at_code;
    default:
        return false;
    }
}

static void riscv_cbqri_bc_write_mon_ctl(RiscvCbqriBandwidthState *bc,
                                         uint64_t value)
{
    if (!bc->caps->supports_mon_op_config_event &&
        !bc->caps->supports_mon_op_read_counter) {
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
                      "%s: busy flag still set, ignored",
                      __func__);
        return;
    }

    if (!bc->caps->supports_at_data &&
        !bc->caps->supports_at_code) {
        /* AT not supported: hardwire to 0 */
        at = 0;
        atv = false;
    }

    if (mcid >= bc->caps->nb_mcids) {
        status = BC_MON_CTL_STATUS_INVAL_MCID;
    } else if (op == BC_MON_OP_CONFIG_EVENT &&
               bc->caps->supports_mon_op_config_event) {
        if (evt_id == BC_EVT_ID_None &&
            bc->caps->supports_mon_evt_id_none) {
            bc->mon_counters[mcid].active = false;
            status = BC_MON_CTL_STATUS_SUCCESS;
        } else if ((evt_id == BC_EVT_ID_RDWR_count &&
                    bc->caps->supports_mon_evt_id_rdwr_count) ||
                   (evt_id == BC_EVT_ID_RDONLY_count &&
                    bc->caps->supports_mon_evt_id_rdonly_count) ||
                   (evt_id == BC_EVT_ID_WRONLY_count &&
                    bc->caps->supports_mon_evt_id_wronly_count)) {
            if (atv && !is_valid_at(bc, at)) {
                status = BC_MON_CTL_STATUS_INVAL_AT;
            } else {
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
               bc->caps->supports_mon_op_read_counter) {
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
    if (bc->caps->nbwblks == 0 ||
        (!bc->caps->supports_alloc_op_config_limit &&
         !bc->caps->supports_alloc_op_read_limit)) {
        /* capacity allocation not supported: leave bc_alloc_ctl set to 0 */
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
                      "%s: busy flag still set, ignored",
                      __func__);
        return;
    }

    bool atv = true;
    if (!bc->caps->supports_at_data &&
        !bc->caps->supports_at_code) {
        /* AT not supported: hardwire to 0 */
        at = 0;
        atv = false;
    }

    if (rcid >= bc->caps->nb_rcids) {
        status = BC_ALLOC_STATUS_INVAL_RCID;
    } else if (atv && !is_valid_at(bc, at)) {
        status = BC_ALLOC_STATUS_INVAL_AT;
    } else if (op == BC_ALLOC_OP_CONFIG_LIMIT &&
               bc->caps->supports_alloc_op_config_limit) {
        status = bandwidth_config(bc, rcid, at, &busy);
    } else if (op == BC_ALLOC_OP_READ_LIMIT &&
               bc->caps->supports_alloc_op_read_limit) {
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
    if (bc->caps->nbwblks == 0) {
        /* capacity allocation not supported: leave bw_alloc set to 0 */
        return;
    }

    BandwidthAllocation *bc_bw_alloc = &bc->bw_allocations[0];

    /* extract writable fields */
    bc_bw_alloc->Rbwb = FIELD_EX64(value, BC_BW_ALLOC, Rbwb);
    bc_bw_alloc->Mweight = FIELD_EX64(value, BC_BW_ALLOC, Mweight);
    bc_bw_alloc->sharedAT = FIELD_EX64(value, BC_BW_ALLOC, sharedAT);
    bc_bw_alloc->useShared = FIELD_EX64(value, BC_BW_ALLOC, useShared);

    if (!bc->caps->supports_at_data &&
        !bc->caps->supports_at_code) {
        /* AT not supported: hardwire to 0 */
        bc_bw_alloc->sharedAT = 0;
        bc_bw_alloc->useShared = false;
    }
}

static void riscv_cbqri_bc_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    RiscvCbqriBandwidthState *bc = opaque;

    assert((addr % 8) == 0);
    assert(size == 8);

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
                      "%s: out of bounds (addr=0x%x)",
                      __func__, (uint32_t)addr);
    }
}

static uint64_t riscv_cbqri_bc_read(void *opaque, hwaddr addr, unsigned size)
{
    RiscvCbqriBandwidthState *bc = opaque;
    uint64_t value = 0;

    assert((addr % 8) == 0);
    assert(size == 8);

    switch (addr) {
    case A_BC_CAPABILITIES:
        value = FIELD_DP64(value, BC_CAPABILITIES, VER_MAJOR,
                           RISCV_CBQRI_VERSION_MAJOR);
        value = FIELD_DP64(value, BC_CAPABILITIES, VER_MINOR,
                           RISCV_CBQRI_VERSION_MINOR);
        value = FIELD_DP64(value, BC_CAPABILITIES, NBWBLKS,
                           bc->caps->nbwblks);
        value = FIELD_DP64(value, BC_CAPABILITIES, MRBWB,
                           bc->caps->mrbwb);
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
        BandwidthAllocation *bc_bw_alloc = &bc->bw_allocations[0];
        value = FIELD_DP64(value, BC_BW_ALLOC, Rbwb, bc_bw_alloc->Rbwb);
        value = FIELD_DP64(value, BC_BW_ALLOC, Mweight, bc_bw_alloc->Mweight);
        value = FIELD_DP64(value, BC_BW_ALLOC, sharedAT, bc_bw_alloc->sharedAT);
        value = FIELD_DP64(value, BC_BW_ALLOC, useShared, bc_bw_alloc->useShared);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out of bounds (addr=0x%x)",
                      __func__, (uint32_t)addr);
    }

    return value;
}

static const MemoryRegionOps riscv_cbqri_bc_ops = {
    .read = riscv_cbqri_bc_read,
    .write = riscv_cbqri_bc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
};

static void riscv_cbqri_bc_realize(DeviceState *dev, Error **errp)
{
    RiscvCbqriBandwidthState *bc = RISCV_CBQRI_BC(dev);

    assert(bc->mon_counters == NULL);
    bc->mon_counters = g_new0(MonitorCounter, bc->caps->nb_mcids);

    assert(bc->bw_allocations == NULL);
    BandwidthAllocation *bw_alloc_end = get_bw_alloc(bc, bc->caps->nb_rcids, 0);
    unsigned int bw_alloc_size = bw_alloc_end - bc->bw_allocations;
    bc->bw_allocations = g_new0(BandwidthAllocation, bw_alloc_size);

    memory_region_init_io(&bc->mmio, OBJECT(dev), &riscv_cbqri_bc_ops,
                          bc, TYPE_RISCV_CBQRI_BC, 4 * 1024);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &bc->mmio);
}

static void riscv_cbqri_bc_reset_enter(Object *obj, ResetType type)
{
    RiscvCbqriBandwidthState *bc = RISCV_CBQRI_BC(obj);

    bc->bc_mon_ctl = 0;
    bc->bc_alloc_ctl = 0;
}

static void riscv_cbqri_bc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = riscv_cbqri_bc_realize;
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.enter = riscv_cbqri_bc_reset_enter;
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

DeviceState *riscv_cbqri_bc_create(hwaddr addr,
                                   const RiscvCbqriBandwidthCaps *caps,
                                   const char *instance_name)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_CBQRI_BC);
    RiscvCbqriBandwidthState *bc = RISCV_CBQRI_BC(dev);

    bc->caps = caps;
    bc->name = instance_name;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    return dev;
}

type_init(riscv_cbqri_bc_register_types)
