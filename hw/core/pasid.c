/*
 * QEMU abstract of Shared Virtual Memory logic
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

#include "qemu/osdep.h"
#include "hw/core/pasid.h"

void iommu_sva_notifier_register(IOMMUSVAContext *sva_ctx,
                                 IOMMUSVANotifier *n,
                                 IOMMUSVANotifyFn fn,
                                 IOMMUSVAEvent event)
{
    n->event = event;
    n->sva_notify = fn;
    QLIST_INSERT_HEAD(&sva_ctx->sva_notifiers, n, node);
    return;
}

void iommu_sva_notifier_unregister(IOMMUSVAContext *sva_ctx,
                                   IOMMUSVANotifier *notifier)
{
    IOMMUSVANotifier *cur, *next;

    QLIST_FOREACH_SAFE(cur, &sva_ctx->sva_notifiers, node, next) {
        if (cur == notifier) {
            QLIST_REMOVE(cur, node);
            break;
        }
    }
}

void iommu_sva_notify(IOMMUSVAContext *sva_ctx, IOMMUSVAEventData *event_data)
{
    IOMMUSVANotifier *cur;

    QLIST_FOREACH(cur, &sva_ctx->sva_notifiers, node) {
        if ((cur->event == event_data->event) && cur->sva_notify) {
            cur->sva_notify(cur, event_data);
        }
    }
}

void iommu_sva_ctx_init(IOMMUSVAContext *sva_ctx)
{
    QLIST_INIT(&sva_ctx->sva_notifiers);
}
