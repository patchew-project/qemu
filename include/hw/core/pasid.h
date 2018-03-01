/*
 * QEMU abstraction of Shared Virtual Memory
 *
 * Copyright (C) 2018 Red Hat Inc.
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

typedef struct IOMMUSVAContext IOMMUSVAContext;

enum IOMMUSVAEvent {
    IOMMU_SVA_EVENT_TLB_INV,
};
typedef enum IOMMUSVAEvent IOMMUSVAEvent;

struct IOMMUSVAEventData {
    IOMMUSVAEvent event;
    uint64_t length;
    void *data;
};
typedef struct IOMMUSVAEventData IOMMUSVAEventData;

typedef struct IOMMUSVANotifier IOMMUSVANotifier;

typedef void (*IOMMUSVANotifyFn)(IOMMUSVANotifier *notifier,
                                 IOMMUSVAEventData *event_data);

typedef struct IOMMUSVATLBEntry IOMMUSVATLBEntry;

/* See address_space_translate: bit 0 is read, bit 1 is write.  */
typedef enum {
    IOMMU_SVA_NONE = 0,
    IOMMU_SVA_RO   = 1,
    IOMMU_SVA_WO   = 2,
    IOMMU_SVA_RW   = 3,
} IOMMUSVAAccessFlags;

#define IOMMU_SVA_ACCESS_FLAG(r, w) (((r) ? IOMMU_SVA_RO : 0) | \
                                     ((w) ? IOMMU_SVA_WO : 0))

struct IOMMUSVATLBEntry {
    AddressSpace    *target_as;
    hwaddr           va;
    hwaddr           translated_addr;
    hwaddr           addr_mask;  /* 0xfff = 4k translation */
    IOMMUSVAAccessFlags perm;
};

typedef struct IOMMUSVAContextOps IOMMUSVAContextOps;
struct IOMMUSVAContextOps {
    /* Return a TLB entry that contains a given address. */
    IOMMUSVATLBEntry (*translate)(IOMMUSVAContext *sva_ctx,
                                  hwaddr addr, bool is_write);
};

struct IOMMUSVANotifier {
    IOMMUSVANotifyFn sva_notify;
    /*
     * What events we are listening to. Let's allow multiple event
     * registrations from beginning.
     */
    IOMMUSVAEvent event;
    QLIST_ENTRY(IOMMUSVANotifier) node;
};

/*
 * This stands for an IOMMU unit. Any translation device should have
 * this struct inside its own structure to make sure it can leverage
 * common IOMMU functionalities.
 */
struct IOMMUSVAContext {
    uint32_t pasid;
    QLIST_HEAD(, IOMMUSVANotifier) sva_notifiers;
    const IOMMUSVAContextOps *sva_ctx_ops;
};

void iommu_sva_notifier_register(IOMMUSVAContext *sva_ctx,
                                 IOMMUSVANotifier *n,
                                 IOMMUSVANotifyFn fn,
                                 IOMMUSVAEvent event);
void iommu_sva_notifier_unregister(IOMMUSVAContext *sva_ctx,
                                   IOMMUSVANotifier *notifier);
void iommu_sva_notify(IOMMUSVAContext *sva_ctx,
                      IOMMUSVAEventData *event_data);

void iommu_sva_ctx_init(IOMMUSVAContext *sva_ctx);

#endif
