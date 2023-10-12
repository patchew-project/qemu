/*
 * QEMU PowerPC pervasive common chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_pervasive.h"
#include "hw/ppc/fdt.h"
#include <libfdt.h>

#define CPLT_CONF0               0x08
#define CPLT_CONF0_OR            0x18
#define CPLT_CONF0_CLEAR         0x28
#define CPLT_CONF1               0x09
#define CPLT_CONF1_OR            0x19
#define CPLT_CONF1_CLEAR         0x29
#define CPLT_STAT0               0x100
#define CPLT_MASK0               0x101
#define CPLT_PROTECT_MODE        0x3FE
#define CPLT_ATOMIC_CLOCK        0x3FF

uint64_t pnv_chiplet_ctrl_read(PnvChipletControlRegs *ctrl_regs, hwaddr reg,
              unsigned size)
{
    uint64_t val = 0xffffffffffffffffull;

    /* CPLT_CTRL0 to CPLT_CTRL5 */
    for (int i = 0; i <= 5; i++) {
        if (reg == i) {
            val = ctrl_regs->cplt_ctrl[i];
            return val;
        } else if ((reg == (i + 0x10)) || (reg == (i + 0x20))) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                           "xscom read at 0x%016lx\n",
                                          __func__, (unsigned long)reg);
            return val;
        }
    }

    switch (reg) {
    case CPLT_CONF0:
        val = ctrl_regs->cplt_cfg0;
        break;
    case CPLT_CONF0_OR:
    case CPLT_CONF0_CLEAR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                   "xscom read at 0x%016lx\n",
                                   __func__, (unsigned long)reg);
        break;
    case CPLT_CONF1:
        val = ctrl_regs->cplt_cfg1;
        break;
    case CPLT_CONF1_OR:
    case CPLT_CONF1_CLEAR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                   "xscom read at 0x%016lx\n",
                                   __func__, (unsigned long)reg);
        break;
    case CPLT_STAT0:
        val = ctrl_regs->cplt_stat0;
        break;
    case CPLT_MASK0:
        val = ctrl_regs->cplt_mask0;
        break;
    case CPLT_PROTECT_MODE:
        val = ctrl_regs->ctrl_protect_mode;
        break;
    case CPLT_ATOMIC_CLOCK:
        val = ctrl_regs->ctrl_atomic_lock;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Chiplet_control_regs: Invalid xscom "
                 "read at 0x%016lx\n", __func__, (unsigned long)reg);
    }
    return val;
}

void pnv_chiplet_ctrl_write(PnvChipletControlRegs *ctrl_regs, hwaddr reg,
                                 uint64_t val, unsigned size)
{
    /* CPLT_CTRL0 to CPLT_CTRL5 */
    for (int i = 0; i <= 5; i++) {
        if (reg == i) {
            ctrl_regs->cplt_ctrl[i] = val;
            return;
        } else if (reg == (i + 0x10)) {
            ctrl_regs->cplt_ctrl[i] |= val;
            return;
        } else if (reg == (i + 0x20)) {
            ctrl_regs->cplt_ctrl[i] &= ~val;
            return;
        }
    }

    switch (reg) {
    case CPLT_CONF0:
        ctrl_regs->cplt_cfg0 = val;
        break;
    case CPLT_CONF0_OR:
        ctrl_regs->cplt_cfg0 |= val;
        break;
    case CPLT_CONF0_CLEAR:
        ctrl_regs->cplt_cfg0 &= ~val;
        break;
    case CPLT_CONF1:
        ctrl_regs->cplt_cfg1 = val;
        break;
    case CPLT_CONF1_OR:
        ctrl_regs->cplt_cfg1 |= val;
        break;
    case CPLT_CONF1_CLEAR:
        ctrl_regs->cplt_cfg1 &= ~val;
        break;
    case CPLT_STAT0:
        ctrl_regs->cplt_stat0 = val;
        break;
    case CPLT_MASK0:
        ctrl_regs->cplt_mask0 = val;
        break;
    case CPLT_PROTECT_MODE:
        ctrl_regs->ctrl_protect_mode = val;
        break;
    case CPLT_ATOMIC_CLOCK:
        ctrl_regs->ctrl_atomic_lock = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Chiplet_control_regs: Invalid xscom "
                       "write at 0x%016lx\n", __func__, (unsigned long)reg);
    }
    return;
}
