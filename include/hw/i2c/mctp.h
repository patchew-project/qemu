#ifndef QEMU_I2C_MCTP_H
#define QEMU_I2C_MCTP_H

#include "qom/object.h"
#include "hw/qdev-core.h"
#include "net/mctp.h"

typedef struct MCTPI2CPacketHeader {
    uint8_t dest;
    uint8_t prot;
    uint8_t byte_count;
    uint8_t source;
} MCTPI2CPacketHeader;

typedef struct MCTPI2CPacket {
    MCTPI2CPacketHeader i2c;
    MCTPPacket          mctp;
} MCTPI2CPacket;

#define i2c_mctp_payload(buf) (buf + offsetof(MCTPI2CPacket, mctp.payload))

#define TYPE_MCTP_I2C_ENDPOINT "mctp-i2c-endpoint"
OBJECT_DECLARE_TYPE(MCTPI2CEndpoint, MCTPI2CEndpointClass, MCTP_I2C_ENDPOINT)

struct MCTPI2CEndpointClass {
    I2CSlaveClass parent_class;

    int (*put_message_bytes)(MCTPI2CEndpoint *mctp, uint8_t *buf, size_t len);
    size_t (*get_message_bytes)(MCTPI2CEndpoint *mctp, uint8_t *buf,
                                size_t maxlen, uint8_t *mctp_flags);

    void (*handle_message)(MCTPI2CEndpoint *mctp);
    void (*reset_message)(MCTPI2CEndpoint *mctp);

    size_t (*get_message_types)(MCTPI2CEndpoint *mctp, uint8_t *data,
                                size_t maxlen);
};

/*
 * Maximum value of the SMBus Block Write "Byte Count" field (8 bits).
 *
 * This is the count of bytes that follow the Byte Count field and up to, but
 * not including, the PEC byte.
 */
#define I2C_MCTP_MAXBLOCK 255

/*
 * Maximum Transmission Unit under I2C.
 *
 * This is for the MCTP Packet Payload (255, subtracting the 4 byte MCTP Packet
 * Header or the 1 byte MCTP/I2C piggy-backed source address).
 */
#define I2C_MCTP_MAXMTU (I2C_MCTP_MAXBLOCK - (sizeof(MCTPPacketHeader) + 1))

/*
 * Maximum length of an MCTP/I2C packet.
 *
 * This is the sum of the three I2C header bytes (Destination target address,
 * Command Code and Byte Count), the maximum number of bytes in a message (255)
 * and the 1 byte Packet Error Code.
 */
#define I2C_MCTP_MAX_LENGTH (3 + I2C_MCTP_MAXBLOCK + 1)

/*
 * Maximum length of an MCTP/I2C Control Message.
 *
 * This is the 64 byte MCTP Baseline Maximum Transmission Unit, adding the
 * combined MCTP/I2C headers and the trailing 1 byte PEC.
 */
#define I2C_MCTP_CONTROL_MAX_LENGTH \
    (sizeof(MCTPI2CPacket) + MCTP_BASELINE_MTU + 1)

typedef enum {
    I2C_MCTP_STATE_IDLE,
    I2C_MCTP_STATE_RX_STARTED,
    I2C_MCTP_STATE_RX,
    I2C_MCTP_STATE_WAIT_TX,
    I2C_MCTP_STATE_TX,
} MCTPState;

typedef enum {
    I2C_MCTP_STATE_TX_START_SEND,
    I2C_MCTP_STATE_TX_SEND_BYTE,
} MCTPTxState;

typedef struct MCTPI2CEndpoint {
    I2CSlave parent_obj;
    I2CBus *i2c;

    MCTPState state;

    /* mctp endpoint identifier */
    uint8_t my_eid;

    uint8_t  buffer[I2C_MCTP_MAX_LENGTH];
    uint64_t pos;
    size_t   len;

    struct {
        MCTPTxState state;
        bool is_control;

        uint8_t eid;
        uint8_t addr;
        uint8_t pktseq;
        uint8_t flags;

        QEMUBH *bh;
    } tx;
} MCTPI2CEndpoint;

void i2c_mctp_schedule_send(MCTPI2CEndpoint *mctp);

#endif /* QEMU_I2C_MCTP_H */
