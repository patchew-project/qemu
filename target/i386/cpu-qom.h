/*
 * QEMU x86 CPU QOM header (target agnostic)
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */
#ifndef QEMU_I386_CPU_QOM_H
#define QEMU_I386_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_X86_CPU    "x86-cpu"
#define TYPE_I386_CPU   "i386-cpu"
#define TYPE_X86_64_CPU "x86_64-cpu"

OBJECT_DECLARE_CPU_TYPE(X86CPU, X86CPUClass, X86_CPU)

OBJECT_DECLARE_CPU_TYPE(I386CPU, X86CPUClass, I386_CPU)
OBJECT_DECLARE_CPU_TYPE(X86_64CPU, X86CPUClass, X86_64_CPU)

#define X86_CPU_TYPE_SUFFIX "-" TYPE_X86_CPU
#define X86_CPU_TYPE_NAME(name) (name X86_CPU_TYPE_SUFFIX)

#define X86_CPU_TYPE_SUFFIX "-" TYPE_X86_CPU
#define X86_CPU_TYPE_NAME(name) (name X86_CPU_TYPE_SUFFIX)

#endif
