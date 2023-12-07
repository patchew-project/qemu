#include "qemu/osdep.h"
#include "sysemu/cpu-timers.h"

/* icount - Instruction Counter API */

int use_icount;

int64_t icount_get_raw(void)
{
    abort();
    return 0;
}
void icount_start_warp_timer(void)
{
    abort();
}
void icount_account_warp_timer(void)
{
    abort();
}
void icount_notify_exit(void)
{
    abort();
}
