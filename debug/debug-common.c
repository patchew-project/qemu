#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/cpu/cluster.h"
#include "hw/boards.h"
#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"
#include "exec/replay-core.h"
#include "exec/hwaddr.h"
#include "qemu/debug.h"
#include "qom/object_interfaces.h"

static void debug_instance_init(Object *obj)
{
}

static void debug_finalize(Object *obj)
{
}

static void debug_class_init(ObjectClass *oc, void *data)
{
}

static const TypeInfo debug_info = {
    .name = TYPE_DEBUG,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DebugState),
    .instance_init = debug_instance_init,
    .instance_finalize = debug_finalize,
    .class_size = sizeof(DebugClass),
    .class_init = debug_class_init
};

static void debug_register_types(void)
{
    type_register_static(&debug_info);
}

type_init(debug_register_types);
