#include "qemu/osdep.h"
#include "sysemu/vmi-intercept.h"

bool vm_introspection_intercept(VMI_intercept_command ic, Error **errp)
{
    return false;
}
