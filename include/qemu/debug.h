#ifndef QEMU_DEBUG_H
#define QEMU_DEBUG_H

#include "qom/object.h"
#include "qemu/typedefs.h"

struct DebugClass {
    ObjectClass parent_class;
    void (*set_stop_cpu)(CPUState *cpu);
};

struct DebugState {
    Object parent_obj;
};

#define TYPE_DEBUG "debug"
OBJECT_DECLARE_TYPE(DebugState, DebugClass, DEBUG)

#endif /* QEMU_DEBUG_H */
