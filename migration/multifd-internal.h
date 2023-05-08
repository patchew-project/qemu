/*
 * Internal Multifd header
 *
 * Copyright (c) 2019-2020 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifdef QEMU_MIGRATION_MULTIFD_INTERNAL_H
#error Only include this header directly
#endif
#define QEMU_MIGRATION_MULTIFD_INTERNAL_H

#ifndef MULTIFD_INTERNAL
#error This header is internal to multifd
#endif

struct MultiFDRecvState {
    MultiFDRecvParams *params;
    /* number of created threads */
    int count;
    /* syncs main thread and channels */
    QemuSemaphore sem_sync;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* multifd ops */
    MultiFDMethods *ops;
};

extern struct MultiFDRecvState *multifd_recv_state;
