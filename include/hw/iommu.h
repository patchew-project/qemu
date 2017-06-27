#ifndef __HW_IOMMU_H__
#define __HW_IOMMU_H__

#include "qemu/typedefs.h"
#include "qapi/qmp/qdict.h"

void arch_iommu_info(Monitor *mon, const QDict *qdict);

#endif
