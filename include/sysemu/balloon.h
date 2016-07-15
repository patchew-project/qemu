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

#include "qapi-types.h"
#include "hw/virtio/virtio-balloon.h"

typedef void (QEMUBalloonEvent)(void *opaque, ram_addr_t target);
typedef void (QEMUBalloonStatus)(void *opaque, BalloonInfo *info);
typedef BalloonReqStatus (QEMUBalloonGetFreePage)(void *opaque,
                                                  unsigned long *bitmap,
                                                  unsigned long len,
                                                  unsigned long req_id);

typedef BalloonReqStatus (QEMUBalloonFreePageReady)(void *opaque,
                                                    unsigned long *req_id);

int qemu_add_balloon_handler(QEMUBalloonEvent *event_func,
                             QEMUBalloonStatus *stat_func,
                             QEMUBalloonGetFreePage *get_free_page_func,
                             QEMUBalloonFreePageReady *free_page_ready_func,
                             void *opaque);
void qemu_remove_balloon_handler(void *opaque);
bool qemu_balloon_is_inhibited(void);
void qemu_balloon_inhibit(bool state);
bool balloon_free_pages_support(void);
BalloonReqStatus balloon_get_free_pages(unsigned long *bitmap,
                                        unsigned long len,
                                        unsigned long req_id);
BalloonReqStatus balloon_free_page_ready(unsigned long *req_id);

#endif
