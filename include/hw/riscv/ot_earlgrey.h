/*
 * QEMU RISC-V Board Compatible with OpenTitan EarlGrey FPGA platform
 *
 * Copyright (c) 2022-2023 Rivos, Inc.
 *
 * Author(s):
 *  Loïc Lefort <loic@rivosinc.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_RISCV_OT_EARLGREY_H
#define HW_RISCV_OT_EARLGREY_H

#include "qom/object.h"

#define TYPE_RISCV_OT_EARLGREY_MACHINE MACHINE_TYPE_NAME("ot-earlgrey")
OBJECT_DECLARE_SIMPLE_TYPE(OtEarlGreyMachineState, RISCV_OT_EARLGREY_MACHINE)

#define TYPE_RISCV_OT_EARLGREY_BOARD "riscv.ot_earlgrey.board"
OBJECT_DECLARE_SIMPLE_TYPE(OtEarlGreyBoardState, RISCV_OT_EARLGREY_BOARD)

#define TYPE_RISCV_OT_EARLGREY_SOC "riscv.ot_earlgrey.soc"
OBJECT_DECLARE_SIMPLE_TYPE(OtEarlGreySoCState, RISCV_OT_EARLGREY_SOC)

#endif /* HW_RISCV_OT_EARLGREY_H */
