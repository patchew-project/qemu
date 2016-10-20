#include "qemu/osdep.h"
#include "qemu-common.h"
#include "monitor/monitor.h"

/* Monitor is defined internally to the real monitor.c, so
 * it's real contents are never accessed when stubs are in use;
 * just a pointer.
 */
struct Monitor {
    int dummy;
};

Monitor *cur_mon;
Monitor stubs_silent_monitor;

bool monitor_cur_is_qmp(void)
{
    return false;
}
