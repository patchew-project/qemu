#include "qemu/osdep.h"
#include "qapi/qmp/dispatch.h"

void qmp_dispatch_exec(const QmpCommand *cmd, bool oob, void *exec_data,
                       QDict *args, QObject **ret, Error **err)
{
    cmd->fn(args, ret, err);
}
