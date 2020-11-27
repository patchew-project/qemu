/*
 * Simple transactions API
 *
 * Copyright (c) 2020 Virtuozzo International GmbH.
 *
 * Author:
 *  Sementsov-Ogievskiy Vladimir <vsementsov@virtuozzo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "qemu/transactions.h"

typedef struct BdrvAction {
    TransactionActionDrv *drv;
    void *opaque;
} BdrvAction;

void tran_prepend(GSList **list, TransactionActionDrv *drv, void *opaque)
{
    BdrvAction *act;

    act = g_new(BdrvAction, 1);
    *act = (BdrvAction) {
        .drv = drv,
        .opaque = opaque
    };

    *list = g_slist_prepend(*list, act);
}

void tran_abort(GSList *list)
{
    GSList *p;

    for (p = list; p != NULL; p = p->next) {
        BdrvAction *act = p->data;

        if (act->drv->abort) {
            act->drv->abort(act->opaque);
        }

        if (act->drv->clean) {
            act->drv->clean(act->opaque);
        }
    }

    g_slist_free_full(list, g_free);
}

void tran_commit(GSList *list)
{
    GSList *p;

    for (p = list; p != NULL; p = p->next) {
        BdrvAction *act = p->data;

        if (act->drv->commit) {
            act->drv->commit(act->opaque);
        }

        if (act->drv->clean) {
            act->drv->clean(act->opaque);
        }
    }

    g_slist_free_full(list, g_free);
}
