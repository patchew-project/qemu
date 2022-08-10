#include "qemu/osdep.h"
#include "qapi/qmp/dispatch.h"

void qmp_dispatch_exec(const QmpCommand *cmd, bool oob, Monitor *cur_mon,
                       QDict *args, QObject **ret, Error **err)
{
    cmd->fn(args, ret, err);
}
