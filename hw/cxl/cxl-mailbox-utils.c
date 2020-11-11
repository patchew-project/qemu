/*
 * CXL Utility library for mailbox interface
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/pci/pci.h"
#include "hw/cxl/cxl.h"

/* 8.2.8.4.5.1 Command Return Codes */
enum {
    RET_SUCCESS                 = 0x0,
    RET_BG_STARTED              = 0x1, /* Background Command Started */
    RET_EINVAL                  = 0x2, /* Invalid Input */
    RET_ENOTSUP                 = 0x3, /* Unsupported */
    RET_ENODEV                  = 0x4, /* Internal Error */
    RET_ERESTART                = 0x5, /* Retry Required */
    RET_EBUSY                   = 0x6, /* Busy */
    RET_MEDIA_DISABLED          = 0x7, /* Media Disabled */
    RET_FW_EBUSY                = 0x8, /* FW Transfer in Progress */
    RET_FW_OOO                  = 0x9, /* FW Transfer Out of Order */
    RET_FW_AUTH                 = 0xa, /* FW Authentication Failed */
    RET_FW_EBADSLT              = 0xb, /* Invalid Slot */
    RET_FW_ROLLBACK             = 0xc, /* Activation Failed, FW Rolled Back */
    RET_FW_REBOOT               = 0xd, /* Activation Failed, Cold Reset Required */
    RET_ENOENT                  = 0xe, /* Invalid Handle */
    RET_EFAULT                  = 0xf, /* Invalid Physical Address */
    RET_POISON_E2BIG            = 0x10, /* Inject Poison Limit Reached */
    RET_EIO                     = 0x11, /* Permanent Media Failure */
    RET_ECANCELED               = 0x12, /* Aborted */
    RET_EACCESS                 = 0x13, /* Invalid Security State */
    RET_EPERM                   = 0x14, /* Incorrect Passphrase */
    RET_EPROTONOSUPPORT         = 0x15, /* Unsupported Mailbox */
    RET_EMSGSIZE                = 0x16, /* Invalid Payload Length */
    RET_MAX                     = 0x17
};

void process_mailbox(CXLDeviceState *cxl_dstate)
{
    uint16_t ret = RET_SUCCESS;
    uint32_t ret_len = 0;
    uint64_t status_reg;

    /*
     * current state of mailbox interface
     *  uint32_t mbox_cap_reg = cxl_dstate->reg_state32[R_CXL_DEV_MAILBOX_CAP];
     *  uint32_t mbox_ctrl_reg = cxl_dstate->reg_state32[R_CXL_DEV_MAILBOX_CTRL];
     *  uint64_t status_reg = *(uint64_t *)&cxl_dstate->reg_state[A_CXL_DEV_MAILBOX_STS];
     */
    uint64_t command_reg =
        *(uint64_t *)&cxl_dstate->mbox_reg_state[A_CXL_DEV_MAILBOX_CMD];

    /* Check if we have to do anything */
    if (!ARRAY_FIELD_EX32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CTRL, DOORBELL)) {
        qemu_log_mask(LOG_UNIMP, "Corrupt internal state for firmware\n");
        return;
    }

    uint8_t cmd_set = FIELD_EX64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND_SET);
    uint8_t cmd = FIELD_EX64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND);
    (void)cmd;
    switch (cmd_set) {
    default:
        ret = RET_ENOTSUP;
    }

    /*
     * Set the return code
     * XXX: it's a 64b register, but we're not setting the vendor, so we can get
     * away with this
     */
    status_reg = FIELD_DP64(0, CXL_DEV_MAILBOX_STS, ERRNO, ret);

    /*
     * Set the return length
     */
    command_reg = FIELD_DP64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND_SET, 0);
    command_reg = FIELD_DP64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND, 0);
    command_reg = FIELD_DP64(command_reg, CXL_DEV_MAILBOX_CMD, LENGTH, ret_len);

    stq_le_p(cxl_dstate->mbox_reg_state + A_CXL_DEV_MAILBOX_CMD, command_reg);
    stq_le_p(cxl_dstate->mbox_reg_state + A_CXL_DEV_MAILBOX_STS, status_reg);

    /* Tell the host we're done */
    ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CTRL,
                     DOORBELL, 0);
}

