/*
 * QEMU Macintosh floppy disk controller emulator (SWIM2)
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
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "hw/block/sony_superdrive.h"
#include "hw/block/swim2.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "trace.h"

#define SWIM2_MMIO_SIZE            0x2000
#define SWIM2_REG_SHIFT            9

#define SWIM2_FIFO_FLAG_MARK       0x01

#define SWIM2_SETUP_INVERT_WRDATA  BIT(0)
#define SWIM2_SETUP_3_5_OUTPUT     BIT(1)
#define SWIM2_SETUP_GCR_MODE       BIT(2)
#define SWIM2_SETUP_CLOCK_DIV_2    BIT(3)
#define SWIM2_SETUP_TEST_MODE      BIT(4)
#define SWIM2_SETUP_IBM_DATA_MODE  BIT(5)
#define SWIM2_SETUP_GCR_WRITES     BIT(6)
#define SWIM2_SETUP_RESERVED       BIT(7)

#define SWIM2_MODE_CLR_FIFO        BIT(0)
#define SWIM2_MODE_ENBL1           BIT(1)
#define SWIM2_MODE_ENBL2           BIT(2)
#define SWIM2_MODE_ACTION          BIT(3)
#define SWIM2_MODE_WRITE           BIT(4)
#define SWIM2_MODE_SIDE            BIT(5)
#define SWIM2_MODE_ALWAYS1         BIT(6)
#define SWIM2_MODE_MOTORON         BIT(7)

#define SWIM2_ERROR_UNDERRUN       BIT(0)
#define SWIM2_ERROR_MARK_IN_DATA   BIT(1)
#define SWIM2_ERROR_OVERRUN        BIT(2)
#define SWIM2_ERROR_SHORT          BIT(3)
#define SWIM2_ERROR_LONG           BIT(4)

/*
 * These are shifted versions of the "SWIM offsets" in HardwareEqu.a in the
 * System 7.1 source.
 */
typedef enum {
    SWIM2_REG_DATA = 0,
    SWIM2_REG_MARK = 1,
    SWIM2_REG_ERROR = 2,
    SWIM2_REG_PARAMETER = 3,
    SWIM2_REG_PHASE = 4,
    SWIM2_REG_SETUP = 5,
    SWIM2_REG_WRITE_ZEROES = 6,
    SWIM2_REG_WRITE_ONES = 7,
} SWIM2Register;

#define SWIM2_REG_WRITE_CRC  SWIM2_REG_ERROR
#define SWIM2_REG_STATUS     SWIM2_REG_WRITE_ZEROES
#define SWIM2_REG_HANDSHAKE  SWIM2_REG_WRITE_ONES

static const char *const swim2_read_reg_names[] = {
    [SWIM2_REG_DATA] = "DATA",
    [SWIM2_REG_MARK] = "MARK",
    [SWIM2_REG_ERROR] = "ERROR",
    [SWIM2_REG_PARAMETER] = "PARAM",
    [SWIM2_REG_PHASE] = "PHASE",
    [SWIM2_REG_SETUP] = "SETUP",
    [SWIM2_REG_STATUS] = "STATUS",
    [SWIM2_REG_HANDSHAKE] = "HANDSHAKE",
};

static const char *const swim2_write_reg_names[] = {
    [SWIM2_REG_DATA] = "DATA",
    [SWIM2_REG_MARK] = "MARK",
    [SWIM2_REG_WRITE_CRC] = "WRITE_CRC",
    [SWIM2_REG_PARAMETER] = "PARAM",
    [SWIM2_REG_PHASE] = "PHASE",
    [SWIM2_REG_SETUP] = "SETUP",
    [SWIM2_REG_WRITE_ZEROES] = "WRITE0",
    [SWIM2_REG_WRITE_ONES] = "WRITE1",
};

static inline const char *swim2_reg_name(const uint8_t reg,
                                         const bool write)
{
    assert(reg < 8);
    const char *const *table = write ? swim2_write_reg_names :
                                       swim2_read_reg_names;
    return table[reg];
}

static void swim2_set_error(SWIM2State *const ctrl, const uint8_t err)
{
    const uint8_t prev = ctrl->error_reg;
    ctrl->error_reg |= err;
    trace_swim2_error_set(prev, ctrl->error_reg, err, ctrl->mode_reg,
                          (ctrl->mode_reg & SWIM2_MODE_WRITE) != 0,
                          ctrl->fifo_count);
}

static void swim2_fifo_clear(SWIM2State *const ctrl)
{
    ctrl->fifo_head = 0;
    ctrl->fifo_tail = 0;
    ctrl->fifo_count = 0;
    trace_swim2_fifo_clear();
}

static bool swim2_fifo_push(SWIM2State *const ctrl, const uint8_t data,
                            const bool is_mark)
{
    if (ctrl->fifo_count >= ARRAY_SIZE(ctrl->fifo)) {
        return false;
    } else {
        ctrl->fifo[ctrl->fifo_tail].data = data;
        ctrl->fifo[ctrl->fifo_tail].is_mark = is_mark;
        ctrl->fifo_tail = (ctrl->fifo_tail + 1) % ARRAY_SIZE(ctrl->fifo);
        ctrl->fifo_count++;
        trace_swim2_fifo_push(ctrl->fifo_count, data, is_mark);
        return true;
    }
}

static bool swim2_fifo_pop(SWIM2State *const ctrl, uint8_t *const data,
                           bool *const is_mark)
{
    if (ctrl->fifo_count == 0) {
        trace_swim2_fifo_pop(ctrl->fifo_count, 0, false, false);
        return false;
    } else {
        SWIM2FIFOEntry *const entry = &ctrl->fifo[ctrl->fifo_head];

        *data = entry->data;
        if (is_mark) *is_mark = entry->is_mark;

        ctrl->fifo_head = (ctrl->fifo_head + 1) % ARRAY_SIZE(ctrl->fifo);
        ctrl->fifo_count--;

        trace_swim2_fifo_pop(ctrl->fifo_count, entry->data, entry->is_mark, true);
        return true;
    }
}

static SonyDrive *swim2_active_drive(SWIM2State *const ctrl)
{
    uint8_t selected;

    if (ctrl->mode_reg & SWIM2_MODE_ENBL1) {
        selected = 0;
    } else if (ctrl->mode_reg & SWIM2_MODE_ENBL2) {
        selected = 1;
    } else {
        return NULL;
    }

    if (ctrl->drives[selected]) {
        return &ctrl->drives[selected]->sony;
    } else {
        return NULL;
    }
}

static void swim2_apply_drive_lines(SWIM2State *const ctrl)
{
    SonyDrive *const active = swim2_active_drive(ctrl);

    for (uint8_t i = 0; i < SWIM2_MAX_FD; i++) {
        SWIM2Drive *const entry = ctrl->drives[i];

        if (!entry) {
            continue;
        }

        SonyDrive *const drive = &entry->sony;

        const uint8_t phases = (ctrl->phase_reg & 0xF) &
                                    (ctrl->phase_reg >> 4);
        const bool head = (ctrl->mode_reg & SWIM2_MODE_SIDE);
        const bool enabled = (drive == active) &&
                             ((ctrl->mode_reg & SWIM2_MODE_MOTORON) != 0);

        sony_drive_set_inputs(drive, phases, head, enabled);
    }
}

static void swim2_fill_fifo_from_drive(SWIM2State *const ctrl)
{
    SonyDrive *const drive = swim2_active_drive(ctrl);

    if (!drive) {
        return;
    }

    while (ctrl->fifo_count < ARRAY_SIZE(ctrl->fifo)) {
        uint8_t data;
        bool is_mark;

        if (!sony_drive_read_byte(drive, &data, &is_mark)) {
            break;
        } else if (!ctrl->wait_for_mark || is_mark) {
            ctrl->wait_for_mark = false;
            const bool did_push = swim2_fifo_push(ctrl, data, is_mark);
            assert(did_push);
        }
    }
}

static void swim2_push_fifo_to_drive(SWIM2State *const ctrl)
{
    SonyDrive *const drive = swim2_active_drive(ctrl);

    if (!drive) {
        /*
         * The Mac ROM does this to measure how quickly we can spit bytes out
         * to a drive.  Just consume the FIFO without error.
         */
        swim2_fifo_clear(ctrl);
    } else {
        uint8_t data;
        bool is_mark;

        while (swim2_fifo_pop(ctrl, &data, &is_mark)) {
            if (ctrl->wait_for_mark && is_mark) {
                ctrl->wait_for_mark = false;
            } else if (!ctrl->wait_for_mark && !is_mark) {
                sony_drive_write_byte(drive, data);
            }
        }
    }
}

static void swim2_update_mode(SWIM2State *const ctrl, const uint8_t mask,
                              const bool set_bits)
{
    const uint8_t prev_mode = ctrl->mode_reg;

    if (set_bits) {
        ctrl->mode_reg |= mask;
    } else {
        ctrl->mode_reg &= ~mask;
        ctrl->mode_reg |= SWIM2_MODE_ALWAYS1;
    }

    if (!set_bits) {
        ctrl->parameter_index = 0;
    }

    if ((mask & SWIM2_MODE_CLR_FIFO) && set_bits) {
        swim2_fifo_clear(ctrl);
    }

    swim2_apply_drive_lines(ctrl);

    const bool prev_action = prev_mode & SWIM2_MODE_ACTION;
    const bool action = ctrl->mode_reg & SWIM2_MODE_ACTION;

    if (!prev_action && action) {
        ctrl->did_handshake = false;
        ctrl->wait_for_mark = !(ctrl->setup_reg & SWIM2_SETUP_GCR_MODE);

        if (ctrl->mode_reg & SWIM2_MODE_WRITE) {
            swim2_push_fifo_to_drive(ctrl);
        } else {
            swim2_fill_fifo_from_drive(ctrl);
        }
    }
}

static void swim2_handle_phase_write(SWIM2State *const ctrl,
                                     const uint8_t value)
{
    ctrl->phase_reg = value;
    swim2_apply_drive_lines(ctrl);
}

static void swim2_handle_setup_write(SWIM2State *const ctrl,
                                     const uint8_t value)
{
    ctrl->setup_reg = value;
}

/*
 * We don't actually do anything with these parameter values other than store
 * them.
 */
static void swim2_handle_parameter_write(SWIM2State *const ctrl,
                                         const uint8_t value)
{
    ctrl->parameter_data[ctrl->parameter_index] = value;
    ctrl->parameter_index = (ctrl->parameter_index + 1) & 0x3;
}

static uint8_t swim2_handle_parameter_read(SWIM2State *const ctrl)
{
    const uint8_t value = ctrl->parameter_data[ctrl->parameter_index];
    ctrl->parameter_index = (ctrl->parameter_index + 1) & 0x3;
    return value;
}

static uint8_t swim2_handle_handshake_read(SWIM2State *const ctrl) {
    SonyDrive *const drive = swim2_active_drive(ctrl);

    ctrl->did_handshake = true;

    const bool empty = ctrl->fifo_count == 0;
    const bool full = ctrl->fifo_count == ARRAY_SIZE(ctrl->fifo);
    const bool error = ctrl->error_reg != 0;
    const bool sense = drive ? sony_drive_read_sense(drive) : true;
    const bool mark_next = ctrl->fifo_count > 0 ?
                                        ctrl->fifo[ctrl->fifo_head].is_mark :
                                        false;

    uint8_t value = 0;
    if (mark_next) value |= BIT(0);
    /* bit 1: 1 when invalid CRC (i.e., never, for us) */
    /* bit 2: rddata, not emulated */
    if (sense)                value |= BIT(3);
    /* bit 4: unused */
    if (error)                value |= BIT(5);

    if ((ctrl->mode_reg & SWIM2_MODE_WRITE) != 0) {
        if (empty)            value |= BIT(6);
        if (!full || error)   value |= BIT(7);
    } else {
        if (full)             value |= BIT(6);
        if (!empty)           value |= BIT(7);
    }

    return value;
}

static uint64_t swim2_read(void *const opaque, const hwaddr addr,
                           const unsigned int size)
{
    SWIM2State *const ctrl = SWIM2(opaque);
    const uint8_t reg = (addr >> SWIM2_REG_SHIFT) & 0x7;
    uint8_t value = 0xff;

    switch (reg) {
    case SWIM2_REG_DATA: {
        bool is_mark;

        if (!swim2_fifo_pop(ctrl, &value, &is_mark)) {
            value = 0xff;
            swim2_set_error(ctrl, SWIM2_ERROR_OVERRUN);
        } else {
            if (is_mark) {
                swim2_set_error(ctrl, SWIM2_ERROR_MARK_IN_DATA);
            }

            swim2_fill_fifo_from_drive(ctrl);
        }

        break;
    }
    case SWIM2_REG_MARK: {
        /*
         * Allow reading data bytes from here; the specs seem unclear on whether
         * this is allowed, but the Mac Sony driver does so.
         */

        if (!ctrl->did_handshake) {
            /*
             * The Mac ROM does something weird when reading an MFM disk.
             * Immediately after setting ACTION, it pulls and discards two bytes
             * from MARK.  To avoid needing to emulate this with timing,
             * simply detect the reads that are not preceded by a HANDSHAKE
             * and give back the garbage they seem to expect.
             */
            value = 0xff;
        } else if (!swim2_fifo_pop(ctrl, &value, NULL)) {
            value = 0xff;
            swim2_set_error(ctrl, SWIM2_ERROR_OVERRUN);
        } else {
            swim2_fill_fifo_from_drive(ctrl);
        }

        break;
    }
    case SWIM2_REG_ERROR:
        value = ctrl->error_reg;
        ctrl->error_reg = 0;
        break;
    case SWIM2_REG_PARAMETER:
        value = swim2_handle_parameter_read(ctrl);
        break;
    case SWIM2_REG_PHASE:
        value = ctrl->phase_reg;
        break;
    case SWIM2_REG_SETUP:
        value = ctrl->setup_reg;
        break;
    case SWIM2_REG_STATUS:
        value = ctrl->mode_reg;
        break;
    case SWIM2_REG_HANDSHAKE:
        value = swim2_handle_handshake_read(ctrl);
        break;
    default:
        value = 0xff;
        break;
    }

    trace_swim2_mmio_read(addr, size, reg, swim2_reg_name(reg, false), value,
                          ctrl->mode_reg, ctrl->setup_reg, ctrl->phase_reg,
                          ctrl->fifo_count);
    return value;
}

static void swim2_write(void *const opaque, const hwaddr addr,
                        const uint64_t data, const unsigned int size)
{
    SWIM2State *const ctrl = SWIM2(opaque);
    const uint8_t reg = (addr >> SWIM2_REG_SHIFT) & 0x7;
    const uint8_t value = data & 0xff;

    switch (reg) {
    case SWIM2_REG_DATA:
        if (!swim2_fifo_push(ctrl, value, 0)) {
            swim2_set_error(ctrl, SWIM2_ERROR_OVERRUN);
        } else if ((ctrl->mode_reg & SWIM2_MODE_ACTION)) {
            swim2_push_fifo_to_drive(ctrl);
        }
        break;
    case SWIM2_REG_MARK:
        if (!swim2_fifo_push(ctrl, value, SWIM2_FIFO_FLAG_MARK)) {
            swim2_set_error(ctrl, SWIM2_ERROR_OVERRUN);
        } else if ((ctrl->mode_reg & SWIM2_MODE_ACTION)) {
            swim2_push_fifo_to_drive(ctrl);
        }
        break;
    case SWIM2_REG_WRITE_CRC:
        /* Nothing to do. */
        break;
    case SWIM2_REG_PARAMETER:
        swim2_handle_parameter_write(ctrl, value);
        break;
    case SWIM2_REG_PHASE:
        swim2_handle_phase_write(ctrl, value);
        break;
    case SWIM2_REG_SETUP:
        swim2_handle_setup_write(ctrl, value);
        break;
    case SWIM2_REG_WRITE_ZEROES:
        swim2_update_mode(ctrl, value, false);
        break;
    case SWIM2_REG_WRITE_ONES:
        swim2_update_mode(ctrl, value, true);
        break;
    default:
        break;
    }

    trace_swim2_mmio_write(addr, size, reg, swim2_reg_name(reg, true), value,
                           ctrl->mode_reg, ctrl->setup_reg, ctrl->phase_reg,
                           ctrl->fifo_count);
}

static const MemoryRegionOps swim2_mmio_ops = {
    .read = swim2_read,
    .write = swim2_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void swim2_change_cb(void *const opaque, const bool load,
                            Error **const errp)
{
    SWIM2Drive *const drive = opaque;

    if (!load) {
        blk_set_perm(drive->conf.blk, 0, BLK_PERM_ALL, &error_abort);
    } else if (!blkconf_apply_backend_options(&drive->conf,
                 !blk_supports_write_perm(drive->conf.blk), false, errp)) {
        return;
    }

    sony_drive_set_block_backend(&drive->sony, drive->conf.blk);
}

static void swim2_drive_realize(DeviceState *const dev, Error **const errp)
{
    SWIM2Drive *const drive = SWIM2_DRIVE(dev);
    SWIM2State *const ctrl = SWIM2(dev->parent_bus->parent);

    if (!drive->conf.blk) {
        drive->conf.blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
        const int ret = blk_attach_dev(drive->conf.blk, dev);
        assert(ret == 0);
    }

    if (drive->unit < 0 || drive->unit >= SWIM2_MAX_FD) {
        error_setg(errp, "unit %d out of range (0-%d)", drive->unit,
                   SWIM2_MAX_FD - 1);
        return;
    }

    if (ctrl->drives[drive->unit]) {
        error_setg(errp, "floppy unit %d already in use", drive->unit);
        return;
    }

    if (!blkconf_blocksizes(&drive->conf, errp)) {
        /* errp set by blkconf_blocksizes(). */
        return;
    }

    if (drive->conf.logical_block_size  != 512 ||
        drive->conf.physical_block_size != 512) {
        error_setg(errp, "physical and logical block size must be 512 for"
                   " floppy");
        return;
    }

    drive->conf.rerror = BLOCKDEV_ON_ERROR_AUTO;
    drive->conf.werror = BLOCKDEV_ON_ERROR_AUTO;

    if (!blkconf_apply_backend_options(&drive->conf,
          !blk_supports_write_perm(drive->conf.blk), false, errp)) {
        /* errp set by blkconf_apply_backend_options(). */
        return;
    }

    if (blk_get_on_error(drive->conf.blk, 0) != BLOCKDEV_ON_ERROR_ENOSPC &&
        blk_get_on_error(drive->conf.blk, 0) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "SWIM2 doesn't support drive option werror");
        return;
    }

    if (blk_get_on_error(drive->conf.blk, 1) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "SWIM2 doesn't support drive option rerror");
        return;
    }

    ctrl->drives[drive->unit] = drive;
    sony_drive_set_block_backend(&drive->sony, drive->conf.blk);

    static const BlockDevOps swim2_block_ops = {
        .change_media_cb = swim2_change_cb,
    };
    blk_set_dev_ops(drive->conf.blk, &swim2_block_ops, drive);
}

static void swim2_init(Object *const obj)
{
    SWIM2State *const ctrl = SWIM2(obj);

    memory_region_init_io(&ctrl->mmio, obj, &swim2_mmio_ops, ctrl,
                          "swim2", SWIM2_MMIO_SIZE);
    sysbus_init_mmio(&ctrl->parent_obj, &ctrl->mmio);

    for (int i = 0; i < SWIM2_MAX_FD; i++) {
        ctrl->drives[i] = NULL;
    }

    memset(ctrl->parameter_data, 0, sizeof ctrl->parameter_data);
    ctrl->parameter_index = 0;
    ctrl->phase_reg = 0;
    ctrl->setup_reg = 0;
    ctrl->mode_reg = SWIM2_MODE_ALWAYS1;
    ctrl->error_reg = 0;
    ctrl->wait_for_mark = false;
    swim2_fifo_clear(ctrl);
}

static void swim2_realize(DeviceState *const dev, Error **const errp)
{
    SWIM2State *const ctrl = SWIM2(dev);
    ctrl->bus = qbus_new(TYPE_SWIM2_BUS, dev, "SWIM2 dummy bus");

    for (int unit = 0; unit < SWIM2_MAX_FD; unit++) {
        DriveInfo *const dinfo = drive_get(IF_FLOPPY, 0, unit);

        if (dinfo) {
            DeviceState *const drive = qdev_new(TYPE_SWIM2_DRIVE);
            qdev_prop_set_int32(drive, "unit", unit);
            qdev_prop_set_drive_err(drive, "drive",
                                    blk_by_legacy_dinfo(dinfo), &error_abort);
            qdev_realize_and_unref(drive, ctrl->bus, &error_abort);
        }
    }
}

/* TODO: support migration */
static void swim2_class_init(ObjectClass *const oc, const void *const opaque)
{
    DeviceClass *const dc = DEVICE_CLASS(oc);
    dc->realize = swim2_realize;
    dc->desc = "Apple Macintosh SWIM2 floppy diskette drive controller";
}

static void swim2_bus_class_init(ObjectClass *const oc,
                                 const void *const opaque)
{
    BusClass *const bc = BUS_CLASS(oc);
    bc->max_dev = 2;
}

/* TODO: support migration */
static void swim2_drive_class_init(ObjectClass *const oc,
                                   const void *const opaque)
{
    DeviceClass *const dc = DEVICE_CLASS(oc);
    dc->bus_type = TYPE_SWIM2_BUS;
    dc->realize = swim2_drive_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    static const Property swim2_drive_properties[] = {
        DEFINE_BLOCK_PROPERTIES(SWIM2Drive, conf),
        DEFINE_PROP_INT32("unit", SWIM2Drive, unit, -1),
    };
    device_class_set_props(dc, swim2_drive_properties);

    dc->desc = "Apple Macintosh SuperDrive floppy diskette drive";
}

static const TypeInfo swim2_info = {
    .name = TYPE_SWIM2,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof (SWIM2State),
    .instance_init = swim2_init,
    .class_init = swim2_class_init,
};

static const TypeInfo swim2_bus_info = {
    .name = TYPE_SWIM2_BUS,
    .parent = TYPE_BUS,
    .instance_size = 0,
    .class_init = swim2_bus_class_init,
};

static const TypeInfo swim2_drive_info = {
    .name = TYPE_SWIM2_DRIVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof (SWIM2Drive),
    .class_init = swim2_drive_class_init,
};

static void swim2_register_types(void)
{
    type_register_static(&swim2_info);
    type_register_static(&swim2_bus_info);
    type_register_static(&swim2_drive_info);
}

type_init(swim2_register_types)
