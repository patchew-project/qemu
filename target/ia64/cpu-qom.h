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

#ifndef __CPu_CLASS_HID
#define __CPu_CLASS_HID

#include "hw/core/cpu.h"

struct IA64CPUClass {
    CPUClass parent_class;
    DeviceRealize parent_realize; 
    ResettablePhases parent_phases;
} __attribute__((aligned(64))); /* Optimization in first local */

#endif