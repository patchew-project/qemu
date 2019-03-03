/*
 * RDMA device: Human Monitor interface
 *
 * Copyright (C) 2019 Oracle
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef RDMA_HMP_H
#define RDMA_HMP_H

#include "qom/object.h"

#define TYPE_RDMA_STATS_PROVIDER "rdma"

#define RDMA_STATS_PROVIDER_CLASS(klass) \
    OBJECT_CLASS_CHECK(RdmaStatsProviderClass, (klass), \
                       TYPE_RDMA_STATS_PROVIDER)
#define RDMA_STATS_PROVIDER_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RdmaStatsProviderClass, (obj), \
                     TYPE_RDMA_STATS_PROVIDER)
#define RDMA_STATS_PROVIDER(obj) \
    INTERFACE_CHECK(RdmaStatsProvider, (obj), \
                    TYPE_RDMA_STATS_PROVIDER)

typedef struct RdmaStatsProvider RdmaStatsProvider;

typedef struct RdmaStatsProviderClass {
    InterfaceClass parent;

    void (*print_statistics)(Monitor *mon, RdmaStatsProvider *obj);
} RdmaStatsProviderClass;

#endif
