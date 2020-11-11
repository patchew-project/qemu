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

static uint64_t mailbox_reg_read(void *opaque, hwaddr offset, unsigned size)
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
    default:
        qemu_log_mask(LOG_UNIMP, "%uB component register read\n", size);
        return 0;
    }

    return ldn_le_p(cxl_dstate->mbox_reg_state + offset, size);
}

static void mailbox_mem_writel(uint32_t *reg_state, hwaddr offset,
                               uint64_t value)
{
    switch (offset) {
    case A_CXL_DEV_MAILBOX_CTRL:
        /* fallthrough */
    case A_CXL_DEV_MAILBOX_CAP:
        /* RO register */
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s Unexpected 32-bit access to 0x%" PRIx64 " (WI)\n",
                      __func__, offset);
        break;
    }

    stl_le_p((uint8_t *)reg_state + offset, value);
}

static void mailbox_mem_writeq(uint64_t *reg_state, hwaddr offset,
                               uint64_t value)
{
    switch (offset) {
    case A_CXL_DEV_MAILBOX_CMD:
        break;
    case A_CXL_DEV_BG_CMD_STS:
        /* BG not supported */
        /* fallthrough */
    case A_CXL_DEV_MAILBOX_STS:
        /* Read only register, will get updated by the state machine */
        return;
    case A_CXL_DEV_MAILBOX_CAP:
    case A_CXL_DEV_MAILBOX_CTRL:
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s Unexpected 64-bit access to 0x%" PRIx64 " (WI)\n",
                      __func__, offset);
        return;
    }

    stq_le_p((uint8_t *)reg_state + offset, value);
}

static void mailbox_reg_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    CXLDeviceState *cxl_dstate = opaque;

    /*
     * Lock is needed to prevent concurrent writes as well as to prevent writes
     * coming in while the firmware is processing. Without background commands
     * or the second mailbox implemented, this serves no purpose since the
     * memory access is synchronized at a higher level (per memory region).
     */
    RCU_READ_LOCK_GUARD();

    switch (size) {
    case 4:
        if (unlikely(offset & (sizeof(uint32_t) - 1))) {
            qemu_log_mask(LOG_UNIMP, "Unaligned register read\n");
            return;
        }
        mailbox_mem_writel(cxl_dstate->mbox_reg_state32, offset, value);
        break;
    case 8:
        if (unlikely(offset & (sizeof(uint64_t) - 1))) {
            qemu_log_mask(LOG_UNIMP, "Unaligned register read\n");
            return;
        }
        mailbox_mem_writeq(cxl_dstate->mbox_reg_state64, offset, value);
        break;
    }

    if (ARRAY_FIELD_EX32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CTRL,
                         DOORBELL))
        process_mailbox(cxl_dstate);
}

static uint64_t mdev_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t retval = 0;

    retval = FIELD_DP64(retval, CXL_MEM_DEV_STS, MEDIA_STATUS, 1);
    retval = FIELD_DP64(retval, CXL_MEM_DEV_STS, MBOX_READY, 1);

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

static const MemoryRegionOps mdev_ops = {
    .read = mdev_reg_read,
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

static const MemoryRegionOps mailbox_ops = {
    .read = mailbox_reg_read,
    .write = mailbox_reg_write,
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
    memory_region_init_io(&cxl_dstate->mailbox, obj, &mailbox_ops, cxl_dstate,
                          "mailbox", CXL_MAILBOX_REGISTERS_LENGTH);
    memory_region_init_io(&cxl_dstate->memory_device, obj, &mdev_ops,
                          cxl_dstate, "memory device caps",
                          CXL_MEMORY_DEVICE_REGISTERS_LENGTH);

    memory_region_add_subregion(&cxl_dstate->device_registers, 0,
                                &cxl_dstate->caps);
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_DEVICE_REGISTERS_OFFSET,
                                &cxl_dstate->device);
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_MAILBOX_REGISTERS_OFFSET, &cxl_dstate->mailbox);
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_MEMORY_DEVICE_REGISTERS_OFFSET,
                                &cxl_dstate->memory_device);
}

static void mailbox_init_common(uint32_t *mbox_regs)
{
    /* 2048 payload size, with no interrupt or background support */
    ARRAY_FIELD_DP32(mbox_regs, CXL_DEV_MAILBOX_CAP, PAYLOAD_SIZE,
                     CXL_MAILBOX_PAYLOAD_SHIFT);
}

void cxl_device_register_init_common(CXLDeviceState *cxl_dstate)
{
    uint32_t *cap_hdrs = cxl_dstate->caps_reg_state32;
    const int cap_count = 3;

    /* CXL Device Capabilities Array Register */
    ARRAY_FIELD_DP32(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_ID, 0);
    ARRAY_FIELD_DP32(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_VERSION, 1);
    ARRAY_FIELD_DP32(cap_hdrs, CXL_DEV_CAP_ARRAY2, CAP_COUNT, cap_count);

    cxl_device_cap_init(cxl_dstate, DEVICE, 1);
    cxl_device_cap_init(cxl_dstate, MAILBOX, 2);
    cxl_device_cap_init(cxl_dstate, MEMORY_DEVICE, 0x4000);

    mailbox_init_common(cxl_dstate->mbox_reg_state32);
}
