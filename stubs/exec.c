
#include "qemu/osdep.h"
#include "exec/cpu-common.h"
#include "qom/cpu.h"

struct CPUTailQ cpus = QTAILQ_HEAD_INITIALIZER(cpus);
