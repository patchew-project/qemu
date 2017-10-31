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

#include "qemu/osdep.h"
#include "hw/core/iommu.h"

void iommu_notifier_register(IOMMUObject *iommu,
                             IOMMUNotifier *n,
                             IOMMUNotifyFn fn,
                             IOMMUEvent event)
{
    n->event = event;
    n->iommu_notify = fn;
    QLIST_INSERT_HEAD(&iommu->iommu_notifiers, n, node);
    return;
}

void iommu_notifier_unregister(IOMMUObject *iommu,
                               IOMMUNotifier *notifier)
{
    IOMMUNotifier *cur, *next;

    QLIST_FOREACH_SAFE(cur, &iommu->iommu_notifiers, node, next) {
        if (cur == notifier) {
            QLIST_REMOVE(cur, node);
            break;
        }
    }
}

void iommu_notify(IOMMUObject *iommu, IOMMUEventData *event_data)
{
    IOMMUNotifier *cur;

    QLIST_FOREACH(cur, &iommu->iommu_notifiers, node) {
        if ((cur->event == event_data->event) && cur->iommu_notify) {
            cur->iommu_notify(cur, event_data);
        }
    }
}
