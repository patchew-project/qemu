/* SPDX-License-Identifier: MIT */

/*
   * QEMU Project for Intel Itanium (IA-64) emulation by 'Software Developer'.
   *
   * Copyright (c) 2026-2028 João (@dev12124 GitHub, @dev GitLab).
   *
   * This file is the CPU Header sign QEMU Models.
   * Author: João Guilherme da Silva Freitas Lima <joaoanandalima@gmail.com>.
   *
   * WARNINGS:
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

#ifndef __CPU_HEADER_MODEL__
#define __CPU_HEADER_MODEL__

#include "cpu-qom.h"
#include "exec/cpu-defs.h"

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

typedef struct CPUArchState {
      uint64_t gr[128];

      uint64_t nat_lo;
      uint64_t nat_hi;

      uint64_t pr;

      uint64_t br[8];

      uint64_t ar[128];

      uint64_t cr[128];

      uint64_t ip;
      uint64_t slot;

      uint64_t cfm;

      uint64_t psr;
} __attribute__((aligned(64))) CPUIA64State; /* Aligned in a Cache Line (I love optimization) */

struct ArchCPU {
    CPUState parent_obj;
    CPUIA64State env;
} __attribute__((aligned(64)));

#define TYPE_IA64_CPU "ia64-cpu"
OBJECT_DECLARE_CPU_TYPE(IA64CPU, IA64CPUClass, IA64_CPU)

static inline void cpu_get_tb_cpu_state(CPUIA64State *env, vaddr *pc,
                                 uint64_t *cs_base, uint32_t *flags)
{
    if(unlikely(!env)||unlikely(!pc)||unlikely(!cs_base)||unlikely(!flags))
        return;

    *pc = env->ip | (env->slot & 0x3);
    *cs_base = 0;
    *flags = 0;
}

#endif