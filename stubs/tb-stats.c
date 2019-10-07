/*
 * TB Stats Stubs
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * This code is licensed under the GNU GPL v2, or later.
 */


#include "qemu/osdep.h"
#include "exec/tb-stats-flags.h"

void enable_collect_tb_stats(void)
{
    return;
}

bool tb_stats_collection_enabled(void)
{
    return false;
}

void set_default_tbstats_flag(uint32_t flags)
{
    return;
}
