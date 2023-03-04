/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * MIPS Trickbox
 */


#ifndef HW_MIPS_TRICKBOX_H
#define HW_MIPS_TRICKBOX_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MIPS_TRICKBOX "mips.trickbox"

typedef struct MIPSTrickboxState MIPSTrickboxState;
DECLARE_INSTANCE_CHECKER(MIPSTrickboxState, MIPS_TRICKBOX,
                         TYPE_MIPS_TRICKBOX)

struct MIPSTrickboxState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
};

#define REG_SIM_CMD 0x0

enum {
    TRICK_PANIC = 1,
    TRICK_HALT = 2,
    TRICK_SUSPEND = 3,
    TRICK_RESET = 4,
    TRICK_FAIL_MIPS = 0x2c00abc1,
    TRICK_PASS_MIPS = 0x2c00abc2,
    TRICK_FAIL_NANOMIPS = 0x80005bc1,
    TRICK_PASS_NANOMIPS = 0x80005bc2
};

#endif
