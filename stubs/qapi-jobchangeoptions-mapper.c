#include "qemu/osdep.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qapi-types-job.h"

bool JobChangeOptions_mapper(JobChangeOptions *opts, JobType *out, Error **errp)
{
    g_assert_not_reached();
}

bool JobComplete_mapper(JobComplete *opts, JobType *out, Error **errp)
{
    g_assert_not_reached();
}
