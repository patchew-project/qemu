/*
 * Cache Object for SMP machine
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Author: Zhao Liu <zhao1.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef SMP_CACHE_H
#define SMP_CACHE_H

#include "qapi/qapi-types-machine-common.h"
#include "qom/object.h"

#define TYPE_SMP_CACHE "smp-cache"
OBJECT_DECLARE_SIMPLE_TYPE(SMPCache, SMP_CACHE)

struct SMPCache {
    Object parent_obj;

    SMPCacheProperty props[SMP_CACHE__MAX];
};

#endif /* SMP_CACHE_H */
