/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal MCTP-over-SMBus/I2C responder (I2C slave) public hooks.
 *
 * This header exists to allow QOM sub-types to extend supported MCTP message
 * types (e.g. NCSI control) without duplicating the base I2C/framing logic.
 */

#ifndef HW_MISC_MCTP_I2C_RESPONDER_H
#define HW_MISC_MCTP_I2C_RESPONDER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <glib.h>
#include "hw/i2c/i2c.h"
#include "qemu/main-loop.h"
#include "qemu/uuid.h"
#include "qom/object.h"

#define TYPE_MCTP_I2C_RESPONDER "mctp-i2c-responder"
OBJECT_DECLARE_TYPE(MctpI2cResponder, MctpI2cResponderClass, MCTP_I2C_RESPONDER)

/* MCTP message types (DSP0236). */
#define MCTP_MSG_TYPE_CTRL         0x00
#define MCTP_MSG_TYPE_PLDM         0x01
#define MCTP_MSG_TYPE_NCSI_CONTROL 0x02

typedef void (*MctpI2cResponderMsgHandler)(MctpI2cResponder *s);

#ifndef MCTP_I2C_RESPONDER_BUF_SZ
#define MCTP_I2C_RESPONDER_BUF_SZ 300
#endif

/* Keep compatibility with existing .c uses. */
#ifndef BUF_SZ
#define BUF_SZ MCTP_I2C_RESPONDER_BUF_SZ
#endif

/* Internal buffers/queues shared by base and subtypes. */
struct mctp_metadata {
    uint8_t dest;
    uint8_t src;
    uint8_t msg_tag;
    uint8_t dest_slave;
    uint8_t source_slave;
};

struct mctp_package {
    struct mctp_metadata metadata;

    GArray *rx_buf;
    bool rx_ready; /* met eom */

    GArray *tx_buf;
};

struct mctp_per_tx {
    uint8_t buf[BUF_SZ];
    size_t len;
    size_t pos;
};

struct MctpI2cResponder {
    /* i2c controller */
    I2CSlave parent_obj;
    QEMUBH *bh;

    /* MCTP rx/tx raw buffer */
    struct mctp_package mctp_package;

    /* I2C rx buffer */
    uint8_t rx[BUF_SZ];
    uint16_t rx_len;

    /* I2C master tx queue */
    GQueue *tx_queue;
    struct mctp_per_tx *active_tx;

    uint8_t master_tx[BUF_SZ];
    uint16_t master_len;
    uint16_t master_pos;

    /* I2C tx buffer, unused for mctp */
    uint8_t tx[BUF_SZ];
    uint16_t tx_len;
    uint16_t tx_pos;

    /* MCTP properties */
    uint8_t eid;
    uint16_t mtu;
    QemuUUID uuid;

    /* message handlers */
    GHashTable *handlers;
    /* key: msg_type (GINT_TO_POINTER(uint8)), value: handler */
};

struct MctpI2cResponderClass {
    I2CSlaveClass parent_class;

    /* Optional hook: (re)initialize message handlers for this instance. */
    void (*init_handlers)(MctpI2cResponder *s);
};

void mctp_i2c_responder_register_msg_handler(
    MctpI2cResponder *s, uint8_t msg_type,
    MctpI2cResponderMsgHandler handler);

/* Minimal helper APIs for out-of-file message handlers. */
const uint8_t *mctp_i2c_responder_get_rx_buf(MctpI2cResponder *s, size_t *len);
bool mctp_i2c_responder_tx_begin(MctpI2cResponder *s, uint8_t msg_type);
bool mctp_i2c_responder_tx_append(MctpI2cResponder *s,
                                  const void *buf, size_t len);

#endif /* HW_MISC_MCTP_I2C_RESPONDER_H */
