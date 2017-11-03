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

#ifndef HW_CORE_IOMMU_H
#define HW_CORE_IOMMU_H

#include "qemu/queue.h"

enum IOMMUEvent {
    IOMMU_EVENT_BIND_PASIDT,
};
typedef enum IOMMUEvent IOMMUEvent;

struct IOMMUEventData {
    IOMMUEvent event;
    uint64_t length;
    void *data;
};
typedef struct IOMMUEventData IOMMUEventData;

typedef struct IOMMUNotifier IOMMUNotifier;

typedef void (*IOMMUNotifyFn)(IOMMUNotifier *notifier,
                              IOMMUEventData *event_data);

struct IOMMUNotifier {
    IOMMUNotifyFn iommu_notify;
    /*
     * What events we are listening to. Let's allow multiple event
     * registrations from beginning.
     */
    IOMMUEvent event;
    QLIST_ENTRY(IOMMUNotifier) node;
};

typedef struct IOMMUObject IOMMUObject;

/*
 * This stands for an IOMMU unit. Any translation device should have
 * this struct inside its own structure to make sure it can leverage
 * common IOMMU functionalities.
 */
struct IOMMUObject {
    QLIST_HEAD(, IOMMUNotifier) iommu_notifiers;
};

void iommu_notifier_register(IOMMUObject *iommu,
                             IOMMUNotifier *n,
                             IOMMUNotifyFn fn,
                             IOMMUEvent event);
void iommu_notifier_unregister(IOMMUObject *iommu,
                               IOMMUNotifier *notifier);
void iommu_notify(IOMMUObject *iommu, IOMMUEventData *event_data);

#endif
