/*
 * QEMU abstract of IOMMU context
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * Authors: Peter Xu <peterx@redhat.com>,
 *          Liu Yi L <yi.l.liu@intel.com>
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
#include "hw/iommu/iommu.h"

void iommu_ctx_notifier_register(IOMMUContext *iommu_ctx,
                                 IOMMUCTXNotifier *n,
                                 IOMMUCTXNotifyFn fn,
                                 IOMMUCTXEvent event)
{
    n->event = event;
    n->iommu_ctx_event_notify = fn;
    QLIST_INSERT_HEAD(&iommu_ctx->iommu_ctx_notifiers, n, node);
    return;
}

void iommu_ctx_notifier_unregister(IOMMUContext *iommu_ctx,
                                   IOMMUCTXNotifier *notifier)
{
    IOMMUCTXNotifier *cur, *next;

    QLIST_FOREACH_SAFE(cur, &iommu_ctx->iommu_ctx_notifiers, node, next) {
        if (cur == notifier) {
            QLIST_REMOVE(cur, node);
            break;
        }
    }
}

void iommu_ctx_event_notify(IOMMUContext *iommu_ctx,
                            IOMMUCTXEventData *event_data)
{
    IOMMUCTXNotifier *cur;

    QLIST_FOREACH(cur, &iommu_ctx->iommu_ctx_notifiers, node) {
        if ((cur->event == event_data->event) &&
                                 cur->iommu_ctx_event_notify) {
            cur->iommu_ctx_event_notify(cur, event_data);
        }
    }
}

void iommu_context_init(IOMMUContext *iommu_ctx)
{
    QLIST_INIT(&iommu_ctx->iommu_ctx_notifiers);
}
