/*
 * ASPEED Caliptra mailbox model (Caliptra 2.x subsystem mode)
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_ASPEED_CPTRA_MBOX_H
#define HW_MISC_ASPEED_CPTRA_MBOX_H

#include "qom/object.h"
#include "hw/core/sysbus.h"

typedef struct Error Error;

/* SRAM size: 2 MiB (matches MCU_MAILBOX0_SRAM_SIZE in caliptra-mcu-sw) */
#define CPTRA_MBOX0_SRAM_SIZE    (2u * 1024u * 1024u)
#define CPTRA_MBOX0_SRAM_WORDS   (CPTRA_MBOX0_SRAM_SIZE / 4)

/* CSR window: 4 KiB (covers all defined registers) */
#define CPTRA_MBOX0_CSR_SIZE     0x1000u

/* CSR register offsets (relative to CSR BAR) */
#define CPTRA_MBOX0_LOCK_OFF            0x000
#define CPTRA_MBOX0_USER_OFF            0x004
#define CPTRA_MBOX0_TARGET_USER_OFF     0x008
#define CPTRA_MBOX0_TARGET_USER_VAL_OFF 0x00C
#define CPTRA_MBOX0_CMD_OFF             0x010
#define CPTRA_MBOX0_DLEN_OFF            0x014
#define CPTRA_MBOX0_EXECUTE_OFF         0x018
#define CPTRA_MBOX0_TARGET_STATUS_OFF   0x01C
#define CPTRA_MBOX0_CMD_STATUS_OFF      0x020
#define CPTRA_MBOX0_HW_STATUS_OFF       0x024

/* CMD_STATUS values */
#define CPTRA_MBOX0_STATUS_BUSY        0
#define CPTRA_MBOX0_STATUS_DATA_READY  1
#define CPTRA_MBOX0_STATUS_COMPLETE    2
#define CPTRA_MBOX0_STATUS_CMD_FAILURE 3

/* SoC agent ID reported in the USER register when the lock is acquired */
#define CPTRA_MBOX0_SOC_USER_ID    1u

/*
 * Caliptra mailbox interface (frontend), implemented as a QOM interface so
 * that backends can deliver an asynchronous response without depending on the
 * concrete frontend device type.
 */
#define TYPE_CPTRA_MBOX_IF "cptra-mbox-if"
typedef struct CptraMboxIfClass CptraMboxIfClass;
DECLARE_CLASS_CHECKERS(CptraMboxIfClass, CPTRA_MBOX_IF, TYPE_CPTRA_MBOX_IF)
typedef struct CptraMboxIf CptraMboxIf;
#define CPTRA_MBOX_IF(obj) \
    INTERFACE_CHECK(CptraMboxIf, (obj), TYPE_CPTRA_MBOX_IF)

struct CptraMboxIfClass {
    InterfaceClass parent;

    /*
     * Called by the peer when a command submitted via handle_execute() has
     * completed. @status is a CPTRA_MBOX0_STATUS_* value; @data/@len carry the
     * response payload to be written back into the mailbox SRAM (@len bytes,
     * @dlen is the reported DLEN).
     */
    void (*complete)(CptraMboxIf *s, uint32_t status, uint32_t dlen,
                     const uint8_t *data, uint32_t len);
};

/*
 * Caliptra mailbox peer (backend) base class.
 */
#define TYPE_CPTRA_MBOX_PEER "cptra-mbox-peer"
OBJECT_DECLARE_TYPE(CptraMboxPeer, CptraMboxPeerClass, CPTRA_MBOX_PEER)

struct CptraMboxPeer {
    DeviceState parent;

    /* Set by the frontend when this peer is linked to it. */
    CptraMboxIf *intf;
};

struct CptraMboxPeerClass {
    DeviceClass parent;

    /*
     * Process a command. @data/@len is a copy of the request payload from the
     * mailbox SRAM. The peer must eventually report completion by calling the
     * interface's complete() method (synchronously or asynchronously).
     */
    void (*handle_execute)(CptraMboxPeer *p, uint32_t cmd, uint32_t dlen,
                           const uint8_t *data, uint32_t len);

    /* Optional: notify the peer of a mailbox reset. */
    void (*handle_reset)(CptraMboxPeer *p);
};

/* Concrete frontend device type. */
#define TYPE_ASPEED_CPTRA_MBOX "aspeed-cptra-mbox"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedCptraMboxState, ASPEED_CPTRA_MBOX)

struct AspeedCptraMboxState {
    SysBusDevice parent_obj;

    /* Linked backend; NULL means no Caliptra peer is present. */
    CptraMboxPeer *peer;

    bool locked;
    bool command_pending;
    bool release_pending;
    uint32_t user;
    uint32_t target_user;
    uint32_t target_user_valid;
    uint32_t cmd;
    uint32_t dlen;
    uint32_t execute;
    uint32_t target_status;
    uint32_t cmd_status;
    uint32_t hw_status;

    uint32_t sram[CPTRA_MBOX0_SRAM_WORDS];

    MemoryRegion sram_mr;
    MemoryRegion csr_mr;
};

bool aspeed_cptra_mbox_set_peer(AspeedCptraMboxState *s, CptraMboxPeer *peer,
                                Error **errp);

/* Concrete external (chardev) peer type. */
#define TYPE_CPTRA_MBOX_PEER_EXTERN "cptra-mbox-peer-extern"

#endif /* HW_MISC_ASPEED_CPTRA_MBOX_H */
