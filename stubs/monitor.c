#include "qemu/osdep.h"
#include "qapi/error.h"
#include "monitor/monitor.h"
#include "../monitor/monitor-internal.h"
#include "qapi/qapi-types-misc.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-types-qom.h"
#include "qapi/qapi-commands-qdev.h"
#include "hw/qdev-core.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "monitor/hmp.h"

#pragma weak hmp_handle_error
#pragma weak cur_mon
#pragma weak monitor_vprintf
#pragma weak monitor_printf
#pragma weak monitor_cur_is_qmp
#pragma weak qmp_device_list_properties

__thread Monitor *cur_mon;

int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    abort();
}

void monitor_init_hmp(Chardev *chr, bool use_readline, Error **errp)
{
}

void monitor_fdsets_cleanup(void)
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
