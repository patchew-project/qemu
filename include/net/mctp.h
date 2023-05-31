#ifndef QEMU_MCTP_H
#define QEMU_MCTP_H

#define MCTP_BASELINE_MTU 64

enum {
    MCTP_H_FLAGS_EOM = 1 << 6,
    MCTP_H_FLAGS_SOM = 1 << 7,
};

#define MCTP_MESSAGE_IC (1 << 7)

/* DSP0236 1.3.0, Figure 4 */
typedef struct MCTPPacketHeader {
    uint8_t version;
    struct {
        uint8_t dest;
        uint8_t source;
    } eid;
    uint8_t flags;
} MCTPPacketHeader;

typedef struct MCTPPacket {
    MCTPPacketHeader hdr;
    uint8_t          payload[];
} MCTPPacket;

#endif /* QEMU_MCTP_H */
