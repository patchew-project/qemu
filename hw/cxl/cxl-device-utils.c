/*
 * CXL Utility library for devices
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/cxl/cxl.h"

static uint64_t caps_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    CXLDeviceState *cxl_dstate = opaque;

    switch (size) {
    case 4:
        if (unlikely(offset & (sizeof(uint32_t) - 1))) {
            qemu_log_mask(LOG_UNIMP, "Unaligned register read\n");
            return 0;
        }
        break;
    case 8:
        if (unlikely(offset & (sizeof(uint64_t) - 1))) {
            qemu_log_mask(LOG_UNIMP, "Unaligned register read\n");
            return 0;
        }
        break;
    }

    return ldn_le_p(cxl_dstate->caps_reg_state + offset, size);
}

static uint64_t dev_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t retval = 0;

    switch (size) {
    case 4:
        if (unlikely(offset & (sizeof(uint32_t) - 1))) {
            qemu_log_mask(LOG_UNIMP, "Unaligned register read\n");
            return 0;
        }
        break;
    case 8:
        if (unlikely(offset & (sizeof(uint64_t) - 1))) {
            qemu_log_mask(LOG_UNIMP, "Unaligned register read\n");
            return 0;
        }
        break;
    }

    return ldn_le_p(&retval, size);
}

static const MemoryRegionOps dev_ops = {
    .read = dev_reg_read,
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps caps_ops = {
    .read = caps_reg_read,
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

void cxl_device_register_block_init(Object *obj, CXLDeviceState *cxl_dstate)
{
    /* This will be a BAR, so needs to be rounded up to pow2 for PCI spec */
    memory_region_init(
        &cxl_dstate->device_registers, obj, "device-registers",
        pow2ceil(CXL_MAILBOX_REGISTERS_LENGTH + CXL_MAILBOX_REGISTERS_OFFSET));

    memory_region_init_io(&cxl_dstate->caps, obj, &caps_ops, cxl_dstate,
                          "cap-array", CXL_DEVICE_REGISTERS_OFFSET - 0);
    memory_region_init_io(&cxl_dstate->device, obj, &dev_ops, cxl_dstate,
                          "device-status", CXL_DEVICE_REGISTERS_LENGTH);

    memory_region_add_subregion(&cxl_dstate->device_registers, 0,
                                &cxl_dstate->caps);
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_DEVICE_REGISTERS_OFFSET,
                                &cxl_dstate->device);
}

void cxl_device_register_init_common(CXLDeviceState *cxl_dstate)
{
    uint32_t *cap_hdrs = cxl_dstate->caps_reg_state32;
    const int cap_count = 1;

    /* CXL Device Capabilities Array Register */
    ARRAY_FIELD_DP32(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_ID, 0);
    ARRAY_FIELD_DP32(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_VERSION, 1);
    ARRAY_FIELD_DP32(cap_hdrs, CXL_DEV_CAP_ARRAY2, CAP_COUNT, cap_count);

    cxl_device_cap_init(cxl_dstate, DEVICE, 1);
}
