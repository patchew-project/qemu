#include "qemu/osdep.h"
#include "qemu-common.h"
#include "exec/gdbstub.h"       /* xml_builtin */

#pragma weak gdbserver_start

const char *const xml_builtin[][2] = {
  { NULL, NULL }
};

#ifdef CONFIG_USER_ONLY

int gdbserver_start(int port)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}

#else

int gdbserver_start(const char *device)
{
    qemu_debug_assert(0);

    return -ENOSYS;
}

#endif
