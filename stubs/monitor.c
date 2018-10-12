#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "monitor/monitor.h"
#include "qapi/qapi-types-misc.h"
#include "qapi/qapi-commands-misc.h"

__thread Monitor *cur_mon;

int monitor_get_fd(Monitor *mon, const char *name, Error **errp)
{
    error_setg(errp, "only QEMU supports file descriptor passing");
    return -1;
}

void monitor_init(Chardev *chr, int flags)
{
}

int monitor_get_cpu_index(void)
{
    return -ENOSYS;
}
void monitor_printf(Monitor *mon, const char *fmt, ...)
{
}

bool monitor_cur_is_qmp(void)
{
    return false;
}

ObjectPropertyInfoList *qmp_device_list_properties(const char *typename,
                                                   Error **errp)
{
    return NULL;
}

void monitor_vfprintf(FILE *stream, const char *fmt, va_list ap)
{
}
