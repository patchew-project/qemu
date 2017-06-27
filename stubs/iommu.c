#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "hw/iommu.h"

void arch_iommu_info(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "This command is not supported "
                   "on this platform.\n");
}
