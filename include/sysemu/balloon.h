/*
 * Balloon
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_BALLOON_H
#define QEMU_BALLOON_H

#include "exec/cpu-common.h"
#include "qapi/qapi-types-machine.h"

typedef void (QEMUBalloonEvent)(void *opaque, ram_addr_t target);
typedef void (QEMUBalloonWSRequest)(void *opaque);
typedef void (QEMUBalloonStatus)(void *opaque, BalloonInfo *info);
typedef void (QEMUBalloonWSConfig)(void *opaque, uint64_t i0, uint64_t i1,
                                   uint64_t i2, uint64_t refresh,
                                   uint64_t report);

int qemu_add_balloon_handler(QEMUBalloonEvent *event_func,
                             QEMUBalloonStatus *stat_func,
                             QEMUBalloonWSRequest *wss_func,
                             QEMUBalloonWSConfig *config_func, void *opaque);
void qemu_remove_balloon_handler(void *opaque);

#endif
