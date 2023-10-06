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
#include "exec/gdbstub.h"
#include "qemu/debug.h"

void gdb_init_debug_class(void)
{
    Object *obj;
    obj = object_new(TYPE_DEBUG);
    DebugState *ds = DEBUG(obj);
    DebugClass *dc = DEBUG_GET_CLASS(ds);
    dc->set_stop_cpu = gdb_set_stop_cpu;
    MachineState *ms = MACHINE(qdev_get_machine());
    ms->debug_state = ds;
}
