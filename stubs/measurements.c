#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "hw/misc/measurements.h"

void print_measurements(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "No measurement support\n");
}

void extend_data(int pcrnum, uint8_t *data, size_t len)
{
    return;
}
