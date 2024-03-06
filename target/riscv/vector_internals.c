/*
 * RISC-V Vector Extension Internals
 *
 * Copyright (c) 2020 T-Head Semiconductor Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "vector_internals.h"

/* set agnostic elements to 1s */
#if HOST_BIG_ENDIAN
void vext_set_elems_1s(void *vd, uint32_t is_agnostic, uint32_t esz,
                       uint32_t idx, uint32_t tot)
{
    if (is_agnostic == 0) {
        /* policy undisturbed */
        return;
    }
    void *base = NULL;
    switch (esz) {
    case 1:
        base = ((int8_t *)vd + H1(idx));
    break;
    case 2:
        base = ((int16_t *)vd + H2(idx));
    break;
    case 4:
        base = ((int32_t *)vd + H4(idx));
    break;
    case 8:
        base = ((int64_t *)vd + H8(idx));
    break;
    default:
        g_assert_not_reached();
    break;
    }
    /*
     * spilt the elements into 2 parts
     * part_begin: the memory need to be set in the first uint64_t unit
     * part_allign: the memory need to be set begins from next uint64_t
     *              unit and alligned to 8
     */
    uint32_t cnt = idx * esz;
    int part_begin, part_allign;
    part_begin = MIN(tot - cnt, 8 - (cnt % 8));
    part_allign = ((tot - cnt - part_begin) / 8) * 8;

    memset(base - part_begin + 1, -1, part_begin);
    memset(QEMU_ALIGN_PTR_UP(base, 8), -1, part_allign);
}
#else
void vext_set_elems_1s(void *vd, uint32_t is_agnostic, uint32_t esz,
                       uint32_t idx, uint32_t tot)
{
    if (is_agnostic == 0) {
        /* policy undisturbed */
        return;
    }
    uint32_t cnt = idx * esz;
    memset(vd + cnt, -1, tot - cnt);
}
#endif

void vext_set_elems_1s_le(void *base, uint32_t is_agnostic, uint32_t cnt,
                       uint32_t tot)
{
    if (is_agnostic == 0) {
        /* policy undisturbed */
        return;
    }
    if (tot - cnt == 0) {
        return ;
    }
    memset(base + cnt, -1, tot - cnt);
}

void do_vext_vv(void *vd, void *v0, void *vs1, void *vs2,
                CPURISCVState *env, uint32_t desc,
                opivv2_fn *fn, uint32_t esz)
{
    uint32_t vm = vext_vm(desc);
    uint32_t vl = env->vl;
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);
    uint32_t vta = vext_vta(desc);
    uint32_t vma = vext_vma(desc);
    uint32_t i;

    for (i = env->vstart; i < vl; i++) {
        if (!vm && !vext_elem_mask(v0, i)) {
            /* set masked-off elements to 1s */
            vext_set_elems_1s(vd, vma, esz, i, (i + 1) * esz);
            continue;
        }
        fn(vd, vs1, vs2, i);
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, esz, vl, total_elems * esz);
}

void do_vext_vx(void *vd, void *v0, target_long s1, void *vs2,
                CPURISCVState *env, uint32_t desc,
                opivx2_fn fn, uint32_t esz)
{
    uint32_t vm = vext_vm(desc);
    uint32_t vl = env->vl;
    uint32_t total_elems = vext_get_total_elems(env, desc, esz);
    uint32_t vta = vext_vta(desc);
    uint32_t vma = vext_vma(desc);
    uint32_t i;

    for (i = env->vstart; i < vl; i++) {
        if (!vm && !vext_elem_mask(v0, i)) {
            /* set masked-off elements to 1s */
            vext_set_elems_1s(vd, vma, esz, i, (i + 1) * esz);
            continue;
        }
        fn(vd, s1, vs2, i);
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, esz, vl, total_elems * esz);
}
