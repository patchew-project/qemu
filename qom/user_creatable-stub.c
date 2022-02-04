#include "qemu/osdep.h"

#include "qom/object_interfaces.h"

bool user_creatable_complete(UserCreatable *uc, Error **errp)
{
    g_assert_not_reached();
}
