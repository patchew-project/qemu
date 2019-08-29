/*
 * QEMU System Emulator, Code Quality Monitor System
 *
 * Copyright (c) 2019 Vanderson M. do Rosario <vandersonmr2@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef TB_STATS_FLAGS
#define TB_STATS_FLAGS

enum TBStatsStatus {
    TB_STATS_DISABLED = 0,
    TB_STATS_RUNNING,
    TB_STATS_PAUSED,
    TB_STATS_STOPPED
};

#define TB_NOTHING    (1 << 0)
#define TB_EXEC_STATS (1 << 1)
#define TB_JIT_STATS  (1 << 2)
#define TB_JIT_TIME   (1 << 3)

extern int tcg_collect_tb_stats;
extern uint32_t default_tbstats_flag;

#endif
