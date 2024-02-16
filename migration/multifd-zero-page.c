/*
 * Multifd zero page detection implementation.
 *
 * Copyright (c) 2024 Bytedance Inc
 *
 * Authors:
 *  Hao Xiang <hao.xiang@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "exec/ramblock.h"
#include "migration.h"
#include "multifd.h"
#include "options.h"
#include "ram.h"

void multifd_zero_page_check_send(MultiFDSendParams *p)
{
    /*
     * QEMU older than 9.0 don't understand zero page
     * on multifd channel. This switch is required to
     * maintain backward compatibility.
     */
    bool use_multifd_zero_page =
        (migrate_zero_page_detection() == ZERO_PAGE_DETECTION_MULTIFD);
    MultiFDPages_t *pages = p->pages;
    RAMBlock *rb = pages->block;

    assert(pages->num != 0);
    assert(pages->normal_num == 0);
    assert(pages->zero_num == 0);

    for (int i = 0; i < pages->num; i++) {
        uint64_t offset = pages->offset[i];
        if (use_multifd_zero_page &&
            buffer_is_zero(rb->host + offset, p->page_size)) {
            pages->zero[pages->zero_num] = offset;
            pages->zero_num++;
            ram_release_page(rb->idstr, offset);
        } else {
            pages->normal[pages->normal_num] = offset;
            pages->normal_num++;
        }
    }
}

void multifd_zero_page_check_recv(MultiFDRecvParams *p)
{
    for (int i = 0; i < p->zero_num; i++) {
        void *page = p->host + p->zero[i];
        if (!buffer_is_zero(page, p->page_size)) {
            memset(page, 0, p->page_size);
        }
    }
}
