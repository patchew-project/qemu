#include "qemu/osdep.h"
#include "qemu-common.h"
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
#include "monitor/qdev.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"

#include "qapi/qapi-types-block-core.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-emit-events.h"

#pragma weak cur_mon
#pragma weak monitor_vprintf
#pragma weak monitor_get_fd
#pragma weak monitor_init
#pragma weak qapi_event_emit
#pragma weak monitor_get_cpu_index
#pragma weak monitor_printf
#pragma weak monitor_cur_is_qmp
#pragma weak qmp_device_list_properties
#pragma weak monitor_init_qmp
#pragma weak monitor_init_hmp

#pragma weak hmp_handle_error
#pragma weak cur_mon
#pragma weak monitor_vprintf
#pragma weak monitor_printf
#pragma weak monitor_cur_is_qmp
#pragma weak qmp_device_list_properties
#pragma weak monitor_fdsets_cleanup

__thread Monitor *cur_mon;

int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
    qemu_debug_assert(0);
    abort();
}

void monitor_init_hmp(Chardev *chr, bool use_readline, Error **errp)
{
    qemu_debug_assert(0);
}

void monitor_fdsets_cleanup(void)
{
    qemu_debug_assert(0);
}

int monitor_get_cpu_index(void)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}
int monitor_printf(Monitor *mon, const char *fmt, ...)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}

bool monitor_cur_is_qmp(void)
{
    qemu_debug_assert(0);

    return false;
}

ObjectPropertyInfoList *qmp_device_list_properties(const char *typename,
                                                   Error **errp)
{
    qemu_debug_assert(0);

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
