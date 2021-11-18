/*
 * dirty restraint helper functions
 *
 * Copyright (c) 2021 CHINA TELECOM CO.,LTD.
 *
 * Authors:
 *  Hyman Huang(黄勇) <huangy81@chinatelecom.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_DIRTYRESTRAINT_H
#define QEMU_DIRTYRESTRAINT_H

#define DIRTYRESTRAINT_CALC_PERIOD_TIME_S   15      /* 15s */

int64_t dirtyrestraint_calc_current(int cpu_index);

void dirtyrestraint_calc_start(void);

void dirtyrestraint_calc_state_init(int max_cpus);
#endif
