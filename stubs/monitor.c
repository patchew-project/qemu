#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-emit-events.h"
#include "monitor/monitor.h"

int monitor_get_fd(Monitor *mon, const char *name, Error **errp)
{
    error_setg(errp, "only QEMU supports file descriptor passing");
    return -1;
}

void monitor_init_hmp(Chardev *chr, bool use_readline, Error **errp)
{
}

void monitor_init_qmp(Chardev *chr, bool pretty, Error **errp)
{
}

void qapi_event_emit(QAPIEvent event, QDict *qdict)
{
}
