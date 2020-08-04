#include "qemu/osdep.h"
#include "qemu/main-loop.h"

void __attribute__((weak)) qemu_notify_event(void)
{
}
