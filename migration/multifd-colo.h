/*
 * multifd colo header
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_MULTIFD_COLO_H
#define QEMU_MIGRATION_MULTIFD_COLO_H

#ifdef CONFIG_REPLICATION

void multifd_colo_prepare_recv_pages(MultiFDRecvParams *p);
void multifd_colo_process_recv_pages(MultiFDRecvParams *p);

#else

static inline void multifd_colo_prepare_recv_pages(MultiFDRecvParams *p) {}
static inline void multifd_colo_process_recv_pages(MultiFDRecvParams *p) {}

#endif
#endif
