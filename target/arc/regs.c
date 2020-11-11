/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "target/arc/regs.h"
#include "target/arc/mmu.h"
#include "target/arc/mpu.h"
#include "target/arc/irq.h"
#include "target/arc/timer.h"
#include "target/arc/cache.h"

struct arc_aux_reg_detail arc_aux_regs_detail[ARC_AUX_REGS_DETAIL_LAST] = {
#define DEF(NUM, CPU, SUB, NAME) \
  { \
    NUM, \
    CPU, \
    SUB, \
    AUX_ID_##NAME, \
    #NAME, \
    sizeof(#NAME) - 1, \
    NULL, \
    NULL, \
  },
#include "target/arc/regs-detail.def"
#undef DEF
};

struct arc_aux_reg arc_aux_regs[ARC_AUX_REGS_LAST] = {
#define AUX_REG(NAME, GET_FUNC, SET_FUNC) \
  { \
    NULL, \
    GET_FUNC, \
    SET_FUNC \
  },
#include "target/arc/regs.def"
#undef AUX_REG
};

const char *arc_aux_reg_name[ARC_AUX_REGS_DETAIL_LAST] = {
#define AUX_REG(NAME, GET, SET) #NAME,
#include "target/arc/regs.def"
#undef AUX_REG
  "last_invalid_aux_reg"
};


void arc_aux_regs_init(void)
{
    int i;

    for (i = 0; i < ARC_AUX_REGS_DETAIL_LAST; i++) {
        enum arc_aux_reg_enum id = arc_aux_regs_detail[i].id;
        struct arc_aux_reg_detail *next = arc_aux_regs[id].first;
        arc_aux_regs_detail[i].next = next;
        arc_aux_regs_detail[i].aux_reg = &(arc_aux_regs[id]);
        arc_aux_regs[id].first = &(arc_aux_regs_detail[i]);
    }
}

int
arc_aux_reg_address_for(enum arc_aux_reg_enum aux_reg_def,
                        int isa_mask)
{
    /* TODO: This must validate for CPU. */
    struct arc_aux_reg_detail *detail = arc_aux_regs[aux_reg_def].first;
    while (detail != NULL) {
        if ((detail->cpu & isa_mask) != 0) {
            return detail->address;
        }
        detail = detail->next;
    }
    assert(0);

    /* We never get here but to accommodate -Werror ... */
    return 0;
}

struct arc_aux_reg_detail *
arc_aux_reg_struct_for_address(int address, int isa_mask)
{
    int i;
    bool has_default = false;
    struct arc_aux_reg_detail *default_ret = NULL;

    /* TODO: Make this a binary search or something faster. */
    for (i = 0; i < ARC_AUX_REGS_DETAIL_LAST; i++) {
        if (arc_aux_regs_detail[i].address == address) {
            if (arc_aux_regs_detail[i].cpu == ARC_OPCODE_DEFAULT) {
                has_default = true;
                default_ret = &(arc_aux_regs_detail[i]);
            } else if ((arc_aux_regs_detail[i].cpu & isa_mask) != 0) {
                return &(arc_aux_regs_detail[i]);
            }
        }
    }

    if (has_default == true) {
        return default_ret;
    }

    return NULL;
}

uint32_t
arc_regs_bcr_default_impl(const struct arc_aux_reg_detail *aux_reg,
                          void *data)
{
    return 0;
}

/* TODO: Implement any non implemented auxs regs. */
void TO_IMPLEMENT_SET(const struct arc_aux_reg_detail *aux_reg, uint32_t val,
                      void *data)
{
    return;
}
uint32_t TO_IMPLEMENT_GET(const struct arc_aux_reg_detail *aux_reg, void *data)
{
    return 0;
}
