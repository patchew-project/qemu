#include "qemu/osdep.h"
#include "hw/core/cpu.h"

CPUTailQ cpus_queue = QTAILQ_HEAD_INITIALIZER(cpus_queue);
