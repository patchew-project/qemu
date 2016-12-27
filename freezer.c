#include "qemu/osdep.h"
#include "qemu/timer.h"

/* These must all initialize to zero */
static int64_t iced_ticks;
static int64_t iced_ns;
static int64_t winter_ticks;
static int64_t winter_ns;

void freeze_time(void)
{
    winter_ticks = cpu_get_host_ticks();
    winter_ns = get_clock();
}

void thaw_time(void)
{
    int64_t ns = winter_ns;
    int64_t ticks = winter_ticks;
    winter_ns = winter_ticks = 0;
    iced_ticks += (cpu_get_host_ticks() - ticks);
    iced_ns += (get_clock() - ns);
}

int64_t ticks_is_frozen(void)
{
    return winter_ticks;
}

int64_t ns_is_frozen(void)
{
    return winter_ns;
}

int64_t get_iced_ticks(void)
{
    return iced_ticks;
}

int64_t get_iced_ns(void)
{
    return iced_ns;
}
