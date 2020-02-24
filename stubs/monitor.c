#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-emit-events.h"
#include "monitor/monitor.h"
#include "qapi/qapi-types-misc.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-types-qom.h"
#include "qapi/qapi-commands-qdev.h"
#include "hw/qdev-core.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "monitor/hmp.h"

__thread Monitor *cur_mon;

#pragma weak hmp_handle_error

int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    abort();
}

int monitor_get_fd(Monitor *mon, const char *name, Error **errp)
{
    error_setg(errp, "only QEMU supports file descriptor passing");
    return -1;
}

void monitor_init_qmp(Chardev *chr, bool pretty)
{
}

void monitor_init_hmp(Chardev *chr, bool use_readline)
{
}

void qapi_event_emit(QAPIEvent event, QDict *qdict)
{
}

int monitor_get_cpu_index(void)
{
    return -ENOSYS;
}
int monitor_printf(Monitor *mon, const char *fmt, ...)
{
    return -ENOSYS;
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

VMChangeStateEntry *qdev_add_vm_change_state_handler(DeviceState *dev,
                                                     VMChangeStateHandler *cb,
                                                     void *opaque)
{
    return NULL;
}

void hmp_handle_error(Monitor *mon, Error *err)
{
}
