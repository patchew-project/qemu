/*
 * 9P Throttle
 *
 * Copyright (C) 2016 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Pradeep Jagadeesh <pradeep.jagadeesh@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 *
 */

#ifndef _9P_THROTTLE_H
#define _9P_THROTTLE_H

#include <stdbool.h>
#include <stdint.h>
#include "block/aio.h"
#include "qemu/main-loop.h"
#include "qemu/coroutine.h"
#include "qemu/throttle.h"

typedef struct FsThrottle {
    ThrottleState ts;
    ThrottleTimers tt;
    AioContext   *aioctx;
    ThrottleConfig cfg;
    bool io_limits_enabled;
    CoQueue      throttled_reqs[2];
    unsigned     pending_reqs[2];
    bool any_timer_armed[2];
    QemuMutex lock;
} FsThrottle;

void throttle9p_enable_io_limits(QemuOpts *, FsThrottle *);

bool throttle9p_get_io_limits_state(FsThrottle *);

void throttle9p_configure_iolimits(QemuOpts *, FsThrottle *);

void throttle9p_request(FsThrottle *, bool , ssize_t);

void throttle9p_cleanup(FsThrottle *);
#endif /* _9P_THROTTLE_H */
