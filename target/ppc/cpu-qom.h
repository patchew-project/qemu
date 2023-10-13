/*
 * QEMU PowerPC CPU
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
#ifndef QEMU_PPC_CPU_QOM_H
#define QEMU_PPC_CPU_QOM_H

#include "hw/core/cpu.h"

#ifdef TARGET_PPC64
#define TYPE_POWERPC_CPU "powerpc64-cpu"
#else
#define TYPE_POWERPC_CPU "powerpc-cpu"
#endif

OBJECT_DECLARE_CPU_TYPE(PowerPCCPU, PowerPCCPUClass, POWERPC_CPU)

#define POWERPC_CPU_TYPE_SUFFIX "-" TYPE_POWERPC_CPU
#define POWERPC_CPU_TYPE_NAME(model) model POWERPC_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_POWERPC_CPU

#define TYPE_HOST_POWERPC_CPU POWERPC_CPU_TYPE_NAME("host")

#endif
