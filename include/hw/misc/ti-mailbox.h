/*
 * TI mailbox (IPC)
 *
 * Copyright (c) 2025 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TI_MAILBOX_H
#define TI_MAILBOX_H

#include "qemu/fifo32.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TI_MAILBOX "ti-mailbox"
OBJECT_DECLARE_SIMPLE_TYPE(TIMailboxState, TI_MAILBOX)

#define TI_MAILBOX_NUM_MBOX 16
#define TI_MAILBOX_NUM_USERS_MAX 4
#define TI_MAILBOX_NUM_USERS_DEFAULT 4
#define TI_MAILBOX_FIFO_DEPTH_MAX 4
#define TI_MAILBOX_FIFO_DEPTH_DEFAULT 4

typedef struct TIMailboxUser {
    uint32_t irq_enable;
    uint32_t raw_set;
    uint32_t raw_clear_mask;
    bool irq_level;
    qemu_irq irq;
} TIMailboxUser;

struct TIMailboxState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    Fifo32 mbox[TI_MAILBOX_NUM_MBOX];
    TIMailboxUser users[TI_MAILBOX_NUM_USERS_MAX];
    uint8_t num_users;
    uint8_t fifo_depth;
    uint8_t mailbox_id;
};

#endif
