/*
 * ASPEED iBT Device
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */
#ifndef ASPEED_IBT_H
#define ASPEED_IBT_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

#define TYPE_ASPEED_IBT "aspeed.ibt"
#define ASPEED_IBT(obj) OBJECT_CHECK(AspeedIBTState, (obj), TYPE_ASPEED_IBT)

#define ASPEED_IBT_NR_REGS (0x1C >> 2)

#define ASPEED_IBT_BUFFER_SIZE 64

typedef struct AspeedIBTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    CharBackend chr;
    bool connected;

    uint8_t recv_msg[ASPEED_IBT_BUFFER_SIZE];
    uint8_t recv_msg_len;
    int recv_msg_index;
    int recv_msg_too_many;
    bool recv_waiting;
    int in_escape;

    uint8_t send_msg[ASPEED_IBT_BUFFER_SIZE];
    uint8_t send_msg_len;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ASPEED_IBT_NR_REGS];

} AspeedIBTState;

#endif /* ASPEED_IBT_H */
