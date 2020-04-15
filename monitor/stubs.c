#include "qemu/osdep.h"
#include "qapi/qmp/qdict.h"

#include "sysemu/vmi-intercept.h"

bool vm_introspection_qmp_delay(void *mon, QDict *rsp)
{
    return false;
}
