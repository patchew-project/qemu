/*
 * QEMU emulation of IOMMU logic
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * Authors: Peter Xu <peterx@redhat.com>,
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

#ifndef __PCI_IOMMU_H__
#define __PCI_IOMMU_H__

#include "qemu/queue.h"

#define IOMMU_EVENT_SVM_PASID      (0)
#define IOMMU_EVENT_MASK           (IOMMU_EVENT_SVM_PASID)

struct IOMMUEvent {
    uint64_t type;
    union {
        struct {
            /* TODO: fill in correct stuff. */
            int value;
        } svm;
    } data;
};
typedef struct IOMMUEvent IOMMUEvent;

typedef struct IOMMUNotifier IOMMUNotifier;

typedef void (*IOMMUNotifyFn)(IOMMUNotifier *notifier, IOMMUEvent *event);

struct IOMMUNotifier {
    IOMMUNotifyFn iommu_notify;
    /*
     * What events we are listening to. Let's allow multiple event
     * registrations from beginning.
     */
    uint64_t event_mask;
    QLIST_ENTRY(IOMMUNotifier) node;
};

/*
 * This stands for an IOMMU unit. Any translation device should have
 * this struct inside its own structure to make sure it can leverage
 * common IOMMU functionalities.
 */
struct IOMMUObject {
    QLIST_HEAD(, IOMMUNotifier) iommu_notifiers;
};
typedef struct IOMMUObject IOMMUObject;

IOMMUNotifier *iommu_notifier_register(IOMMUObject *iommu,
                                       IOMMUNotifyFn fn,
                                       uint64_t event_mask);
void iommu_notifier_unregister(IOMMUObject *iommu,
                               IOMMUNotifier *notifier);
void iommu_notify(IOMMUObject *iommu, IOMMUEvent *event);

#endif
