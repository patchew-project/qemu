/*
 * Simple transactions API
 *
 * Copyright (c) 2020 Virtuozzo International GmbH.
 *
 * Author:
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
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

#ifndef QEMU_TRANSACTIONS_H
#define QEMU_TRANSACTIONS_H

#include <gmodule.h>

typedef struct TransactionActionDrv {
    void (*abort)(void *opeque);
    void (*commit)(void *opeque);
    void (*clean)(void *opeque);
} TransactionActionDrv;

void tran_prepend(GSList **list, TransactionActionDrv *drv, void *opaque);
void tran_abort(GSList *backup);
void tran_commit(GSList *backup);
static inline void tran_finalize(GSList *backup, int ret)
{
    if (ret < 0) {
        tran_abort(backup);
    } else {
        tran_commit(backup);
    }
}

#endif /* QEMU_TRANSACTIONS_H */
