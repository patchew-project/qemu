/*
 * Minimal NXP EdgeLock Enclave (ELE) responder stub
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Watches an i.MX MU for ELE-protocol command words, dispatches them
 * to handlers, and writes responses back through the MU's RR
 * registers. Implements only ele_get_info() (the one ELE call
 * U-Boot SPL's imx9_probe_mu() makes pre-relocation). Everything
 * else returns ELE_OK with a stub response so SPL doesn't panic.
 */

#ifndef IMX95_ELE_SERVER_H
#define IMX95_ELE_SERVER_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "hw/misc/imx_mu.h"

#define TYPE_IMX95_ELE_SERVER "imx95.ele-server"
OBJECT_DECLARE_SIMPLE_TYPE(IMX95ELEServerState, IMX95_ELE_SERVER)

/* ELE protocol constants from arch/arm/include/asm/mach-imx/ele_api.h. */
#define ELE_VERSION                 0x06
#define ELE_CMD_TAG                 0x17
#define ELE_RESP_TAG                0xE1
#define ELE_GET_INFO_REQ            0xDA
#define ELE_SUCCESS_IND             0xD6

/* Maximum words in a single ELE message (per ELE_MAX_MSG in U-Boot). */
#define IMX95_ELE_MAX_WORDS         32

struct IMX95ELEServerState {
    SysBusDevice    parent_obj;

    /* Link to the MU used as ELE transport (elemu1). */
    IMXMUState     *mu;

    /* Accumulator for incoming command words. */
    uint32_t        msg_buf[IMX95_ELE_MAX_WORDS];
    uint32_t        msg_count;
    uint32_t        msg_size;
};

#endif /* IMX95_ELE_SERVER_H */
