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
void vext_set_elems_1s(void *base, uint32_t is_agnostic, uint32_t cnt,
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

/*
 * This function is sensitive to env->vstart changes since
 * it'll be a no-op if vstart >= vl. Do not clear env->vstart
 * before calling it unless you're certain that vstart < vl.
 */
void vext_set_tail_elems_1s(CPURISCVState *env, void *vd, uint32_t desc,
                            uint32_t esz, uint32_t max_elems)
{
    uint32_t vta = vext_vta(desc);
    uint32_t nf = vext_nf(desc);
    int k;

    /*
     * Section 5.4 of the RVV spec mentions:
     * "When vstart ≥ vl, there are no body elements, and no
     *  elements are updated in any destination vector register
     *  group, including that no tail elements are updated
     *  with agnostic values."
     */
    if (vta == 0 || env->vstart >= env->vl) {
        return;
    }

    for (k = 0; k < nf; ++k) {
        vext_set_elems_1s(vd, vta, (k * max_elems + env->vl) * esz,
                          (k * max_elems + max_elems) * esz);
    }
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
            vext_set_elems_1s(vd, vma, i * esz, (i + 1) * esz);
            continue;
        }
        fn(vd, vs1, vs2, i);
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, vl * esz, total_elems * esz);
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
            vext_set_elems_1s(vd, vma, i * esz, (i + 1) * esz);
            continue;
        }
        fn(vd, s1, vs2, i);
    }
    env->vstart = 0;
    /* set tail elements to 1s */
    vext_set_elems_1s(vd, vta, vl * esz, total_elems * esz);
}
