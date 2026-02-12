/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_TAG_ARCH_TABLE_H
#define HEXAGON_TAG_ARCH_TABLE_H

struct tag_rev_info { int introduced, removed; };

static const struct tag_rev_info tag_rev_info[XX_LAST_OPCODE] = {
    [A7_clip] = { .introduced = 0x67, .removed = 0x0 },
    [A7_croundd_ri] = { .introduced = 0x67, .removed = 0x0 },
    [A7_croundd_rr] = { .introduced = 0x67, .removed = 0x0 },
    [A7_vclip] = { .introduced = 0x67, .removed = 0x0 },
    [F2_dfmax] = { .introduced = 0x67, .removed = 0x0 },
    [F2_dfmin] = { .introduced = 0x67, .removed = 0x0 },
    [F2_dfmpyfix] = { .introduced = 0x67, .removed = 0x0 },
    [F2_dfmpyhh] = { .introduced = 0x67, .removed = 0x0 },
    [F2_dfmpylh] = { .introduced = 0x67, .removed = 0x0 },
    [F2_dfmpyll] = { .introduced = 0x67, .removed = 0x0 },
    [J2_callrh] = { .introduced = 0x73, .removed = 0x0 },
    [J2_jumprh] = { .introduced = 0x73, .removed = 0x0 },
    [L2_loadw_aq] = { .introduced = 0x68, .removed = 0x0 },
    [L4_loadd_aq] = { .introduced = 0x68, .removed = 0x0 },
    [M7_dcmpyiw] = { .introduced = 0x67, .removed = 0x0 },
    [M7_dcmpyiw_acc] = { .introduced = 0x67, .removed = 0x0 },
    [M7_dcmpyiwc] = { .introduced = 0x67, .removed = 0x0 },
    [M7_dcmpyiwc_acc] = { .introduced = 0x67, .removed = 0x0 },
    [M7_dcmpyrw] = { .introduced = 0x67, .removed = 0x0 },
    [M7_dcmpyrw_acc] = { .introduced = 0x67, .removed = 0x0 },
    [M7_dcmpyrwc] = { .introduced = 0x67, .removed = 0x0 },
    [M7_dcmpyrwc_acc] = { .introduced = 0x67, .removed = 0x0 },
    [M7_wcmpyiw] = { .introduced = 0x67, .removed = 0x0 },
    [M7_wcmpyiw_rnd] = { .introduced = 0x67, .removed = 0x0 },
    [M7_wcmpyiwc] = { .introduced = 0x67, .removed = 0x0 },
    [M7_wcmpyiwc_rnd] = { .introduced = 0x67, .removed = 0x0 },
    [M7_wcmpyrw] = { .introduced = 0x67, .removed = 0x0 },
    [M7_wcmpyrw_rnd] = { .introduced = 0x67, .removed = 0x0 },
    [M7_wcmpyrwc] = { .introduced = 0x67, .removed = 0x0 },
    [M7_wcmpyrwc_rnd] = { .introduced = 0x67, .removed = 0x0 },
    [R6_release_at_vi] = { .introduced = 0x68, .removed = 0x0 },
    [R6_release_st_vi] = { .introduced = 0x68, .removed = 0x0 },
    [S2_storew_rl_at_vi] = { .introduced = 0x68, .removed = 0x0 },
    [S2_storew_rl_st_vi] = { .introduced = 0x68, .removed = 0x0 },
    [S4_stored_rl_at_vi] = { .introduced = 0x68, .removed = 0x0 },
    [S4_stored_rl_st_vi] = { .introduced = 0x68, .removed = 0x0 },
    [V6_v6mpyhubs10] = { .introduced = 0x68, .removed = 0x0 },
    [V6_v6mpyhubs10_vxx] = { .introduced = 0x68, .removed = 0x0 },
    [V6_v6mpyvubs10] = { .introduced = 0x68, .removed = 0x0 },
    [V6_v6mpyvubs10_vxx] = { .introduced = 0x68, .removed = 0x0 },
    [V6_vasrvuhubrndsat] = { .introduced = 0x69, .removed = 0x0 },
    [V6_vasrvuhubsat] = { .introduced = 0x69, .removed = 0x0 },
    [V6_vasrvwuhrndsat] = { .introduced = 0x69, .removed = 0x0 },
    [V6_vasrvwuhsat] = { .introduced = 0x69, .removed = 0x0 },
    [V6_vassign_tmp] = { .introduced = 0x69, .removed = 0x0 },
    [V6_vcombine_tmp] = { .introduced = 0x69, .removed = 0x0 },
    [V6_vmpyuhvs] = { .introduced = 0x69, .removed = 0x0 },
};

#endif /* HEXAGON_TAG_ARCH_TABLE_H */
