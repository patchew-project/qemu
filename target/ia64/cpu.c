/* SPDX-License-Identifier: MIT */

/*
   * QEMU Project for Intel Itanium (IA-64) emulation by 'Software Developer'.
   *
   * Copyright (c) 2026-2028 João (@dev12124 GitHub, @dev GitLab).
   *
   * This file is Implementation CPU Emulated of IA64 Arch.
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

#include "qemu/osdep.h" 
#include "cpu-qom.h" /* target/ia64/ */
#include "cpu.h" /* target/ia64/ */

static void ia64_set_pc(CPUState *cs, vaddr value)
{
    if(unlikely(!cs)) /* Pure Optimizatiom */
        return;

    CPUIA64State *env = cpu_env(cs); /* Extract */
    env->ip = env->ip | value & ~0xFULL;
    env->slot = value & 0x3;
}

static vaddr ia64_get_pc(CPUState *cs)
{
    if(unlikely(!cs))
        return 0;

    CPUIA64State *env = cpu_env(cs);
    vaddr result = env->ip | (env->slot & 0x3);
    return result;
}