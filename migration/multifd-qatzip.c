/*
 * Multifd QATzip compression implementation
 *
 * Copyright (c) Bytedance
 *
 * Authors:
 *  Bryan Zhang <bryan.zhang@bytedance.com>
 *  Hao Xiang   <hao.xiang@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "exec/ramblock.h"
#include "exec/target_page.h"
#include "qapi/error.h"
#include "migration.h"
#include "options.h"
#include "multifd.h"

static int qatzip_send_setup(MultiFDSendParams *p, Error **errp)
{
    return 0;
}

static void qatzip_send_cleanup(MultiFDSendParams *p, Error **errp) {};

static int qatzip_send_prepare(MultiFDSendParams *p, Error **errp)
{
    MultiFDPages_t *pages = p->pages;

    for (int i = 0; i < p->normal_num; i++) {
        p->iov[p->iovs_num].iov_base = pages->block->host + p->normal[i];
        p->iov[p->iovs_num].iov_len = p->page_size;
        p->iovs_num++;
    }

    p->next_packet_size = p->normal_num * p->page_size;
    p->flags |= MULTIFD_FLAG_NOCOMP;
    return 0;
}

static int qatzip_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    return 0;
}

static void qatzip_recv_cleanup(MultiFDRecvParams *p) {};

static int qatzip_recv_pages(MultiFDRecvParams *p, Error **errp)
{
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;

    if (flags != MULTIFD_FLAG_NOCOMP) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_NOCOMP);
        return -1;
    }
    for (int i = 0; i < p->normal_num; i++) {
        p->iov[i].iov_base = p->host + p->normal[i];
        p->iov[i].iov_len = p->page_size;
    }
    return qio_channel_readv_all(p->c, p->iov, p->normal_num, errp);
}

static MultiFDMethods multifd_qatzip_ops = {
    .send_setup = qatzip_send_setup,
    .send_cleanup = qatzip_send_cleanup,
    .send_prepare = qatzip_send_prepare,
    .recv_setup = qatzip_recv_setup,
    .recv_cleanup = qatzip_recv_cleanup,
    .recv_pages = qatzip_recv_pages
};

static void multifd_qatzip_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_QATZIP, &multifd_qatzip_ops);
}

migration_init(multifd_qatzip_register);
