/*
 * QEMU abstraction of IOMMU Context
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * Authors: Peter Xu <peterx@redhat.com>,
 *          Liu, Yi L <yi.l.liu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_PCI_PASID_H
#define HW_PCI_PASID_H

#include "qemu/queue.h"
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif

typedef struct IOMMUContext IOMMUContext;

enum IOMMUCTXEvent {
    IOMMU_CTX_EVENT_PASID_ALLOC,
    IOMMU_CTX_EVENT_PASID_FREE,
    IOMMU_CTX_EVENT_NUM,
};
typedef enum IOMMUCTXEvent IOMMUCTXEvent;

union IOMMUCTXPASIDReqDesc {
    struct {
        uint32_t min_pasid;
        uint32_t max_pasid;
        int32_t alloc_result; /* pasid allocated for the alloc request */
    };
    struct {
        uint32_t pasid; /* pasid to be free */
        int free_result;
    };
};
typedef union IOMMUCTXPASIDReqDesc IOMMUCTXPASIDReqDesc;

struct IOMMUCTXEventData {
    IOMMUCTXEvent event;
    uint64_t length;
    void *data;
};
typedef struct IOMMUCTXEventData IOMMUCTXEventData;

typedef struct IOMMUCTXNotifier IOMMUCTXNotifier;

typedef void (*IOMMUCTXNotifyFn)(IOMMUCTXNotifier *notifier,
                                 IOMMUCTXEventData *event_data);

struct IOMMUCTXNotifier {
    IOMMUCTXNotifyFn iommu_ctx_event_notify;
    /*
     * What events we are listening to. Let's allow multiple event
     * registrations from beginning.
     */
    IOMMUCTXEvent event;
    QLIST_ENTRY(IOMMUCTXNotifier) node;
};

/*
 * This is an abstraction of IOMMU context.
 */
struct IOMMUContext {
    uint32_t pasid;
    QLIST_HEAD(, IOMMUCTXNotifier) iommu_ctx_notifiers;
};

void iommu_ctx_notifier_register(IOMMUContext *iommu_ctx,
                                 IOMMUCTXNotifier *n,
                                 IOMMUCTXNotifyFn fn,
                                 IOMMUCTXEvent event);
void iommu_ctx_notifier_unregister(IOMMUContext *iommu_ctx,
                                   IOMMUCTXNotifier *notifier);
void iommu_ctx_event_notify(IOMMUContext *iommu_ctx,
                            IOMMUCTXEventData *event_data);

void iommu_context_init(IOMMUContext *iommu_ctx);

#endif
