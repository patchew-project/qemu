/*
 *  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "qemu/osdep.h"
#include "iclass.h"

static const SlotMask iclass_info[] = {

#define DEF_PP_ICLASS32(TYPE, SLOTS, UNITS) \
    [ICLASS_FROM_TYPE(TYPE)] = SLOTS_##SLOTS,
#define DEF_EE_ICLASS32(TYPE, SLOTS, UNITS) \
    [ICLASS_FROM_TYPE(TYPE)] = SLOTS_##SLOTS,
#include "imported/iclass.def"
#undef DEF_PP_ICLASS32
#undef DEF_EE_ICLASS32
};

SlotMask find_iclass_slots(Opcode opcode, int itype)
{
    /* There are some exceptions to what the iclass dictates */
    if (GET_ATTRIB(opcode, A_ICOP)) {
        return SLOTS_2;
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT0ONLY)) {
        return SLOTS_0;
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT1ONLY)) {
        return SLOTS_1;
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT2ONLY)) {
        return SLOTS_2;
    } else if (GET_ATTRIB(opcode, A_RESTRICT_SLOT3ONLY)) {
        return SLOTS_3;
    } else if (GET_ATTRIB(opcode, A_COF) &&
               GET_ATTRIB(opcode, A_INDIRECT) &&
               !GET_ATTRIB(opcode, A_MEMLIKE) &&
               !GET_ATTRIB(opcode, A_MEMLIKE_PACKET_RULES)) {
        return SLOTS_2;
    } else if (GET_ATTRIB(opcode, A_RESTRICT_NOSLOT1)) {
        return SLOTS_0;
    } else if ((opcode == J2_trap0) ||
               (opcode == Y2_isync) ||
               (opcode == J2_pause)) {
        return SLOTS_2;
    } else if (opcode == J4_hintjumpr) {
        return SLOTS_23;
    } else if (GET_ATTRIB(opcode, A_CRSLOT23)) {
        return SLOTS_23;
    } else if (GET_ATTRIB(opcode, A_RESTRICT_PREFERSLOT0)) {
        return SLOTS_0;
    } else if (GET_ATTRIB(opcode, A_SUBINSN)) {
        return SLOTS_01;
    } else if (GET_ATTRIB(opcode, A_CALL)) {
        return SLOTS_23;
    } else if ((opcode == J4_jumpseti) || (opcode == J4_jumpsetr)) {
        return SLOTS_23;
    } else {
        return iclass_info[itype];
    }
}
