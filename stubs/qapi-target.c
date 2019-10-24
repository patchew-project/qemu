#include "qemu/osdep.h"
#include "qemu-common.h"

#include "qapi/qapi-types-misc-target.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qapi-types-machine-target.h"
#include "qapi/qapi-commands-machine-target.h"

void qmp_rtc_reset_reinjection(Error **errp)
{
    qemu_debug_assert(0);
}

SevInfo *qmp_query_sev(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

SevLaunchMeasureInfo *qmp_query_sev_launch_measure(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

SevCapability *qmp_query_sev_capabilities(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

CpuModelExpansionInfo *qmp_query_cpu_model_expansion(CpuModelExpansionType type,
                                                     CpuModelInfo *model,
                                                     Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}
