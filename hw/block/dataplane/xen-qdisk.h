/*
 * Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 */

#ifndef HW_BLOCK_DATAPLANE_QDISK_H
#define HW_BLOCK_DATAPLANE_QDISK_H

#include "hw/xen/xen-bus.h"
#include "sysemu/iothread.h"

typedef struct XenBlkDev XenQdiskDataPlane;

XenQdiskDataPlane *xen_qdisk_dataplane_create(XenDevice *xendev,
                                              BlockConf *conf,
                                              IOThread *iothread);
void xen_qdisk_dataplane_destroy(XenQdiskDataPlane *dataplane);
void xen_qdisk_dataplane_start(XenQdiskDataPlane *dataplane,
                               const unsigned int ring_ref[],
                               unsigned int nr_ring_ref,
                               unsigned int event_channel,
                               unsigned int protocol);
void xen_qdisk_dataplane_stop(XenQdiskDataPlane *dataplane);

#endif /* HW_BLOCK_DATAPLANE_QDISK_H */
