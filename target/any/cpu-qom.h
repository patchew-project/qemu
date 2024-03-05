/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_DUMMY_CPU_QOM_H
#define QEMU_DUMMY_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_DUMMY_CPU "dummy-cpu"

OBJECT_DECLARE_CPU_TYPE(DUMMYCPU, CPUClass, DUMMY_CPU)

#endif
