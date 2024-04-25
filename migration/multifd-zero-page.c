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

static bool multifd_zero_page_enabled(void)
{
    return migrate_zero_page_detection() == ZERO_PAGE_DETECTION_MULTIFD;
}

static void swap_page_offset(ram_addr_t *pages_offset, int a, int b)
{
    ram_addr_t temp;

    if (a == b) {
        return;
    }

    temp = pages_offset[a];
    pages_offset[a] = pages_offset[b];
    pages_offset[b] = temp;
}

/**
 * zero_page_detect_cpu: Perform zero page detection using CPU.
 *
 * Sorts normal pages before zero pages in p->pages->offset and updates
 * p->pages->normal_num.
 *
 * @param p A pointer to the send params.
 */
static void zero_page_detect_cpu(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = p->pages;
    RAMBlock *rb = pages->block;
    int i = 0;
    int j = pages->num - 1;

    /*
     * Sort the page offset array by moving all normal pages to
     * the left and all zero pages to the right of the array.
     */
    while (i <= j) {
        uint64_t offset = pages->offset[i];

        if (!buffer_is_zero(rb->host + offset, p->page_size)) {
            i++;
            continue;
        }

        swap_page_offset(pages->offset, i, j);
        ram_release_page(rb->idstr, offset);
        j--;
    }

    pages->normal_num = i;
}


#ifdef CONFIG_DSA_OPT

static void swap_result(bool *results, int a, int b)
{
    bool temp;

    if (a == b) {
        return;
    }

    temp = results[a];
    results[a] = results[b];
    results[b] = temp;
}

/**
 * zero_page_detect_dsa: Perform zero page detection using
 * Intel Data Streaming Accelerator (DSA).
 *
 * Sorts normal pages before zero pages in p->pages->offset and updates
 * p->pages->normal_num.
 *
 * @param p A pointer to the send params.
 */
static void zero_page_detect_dsa(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = p->pages;
    RAMBlock *rb = pages->block;
    bool *results = p->batch_task->results;

    for (int i = 0; i < p->pages->num; i++) {
        p->batch_task->addr[i] = (ram_addr_t)(rb->host + p->pages->offset[i]);
    }

    buffer_is_zero_dsa_batch_async(p->batch_task,
                                   (const void **)p->batch_task->addr,
                                   p->pages->num,
                                   p->page_size);

    int i = 0;
    int j = pages->num - 1;

    /*
     * Sort the page offset array by moving all normal pages to
     * the left and all zero pages to the right of the array.
     */
    while (i <= j) {
        uint64_t offset = pages->offset[i];

        if (!results[i]) {
            i++;
            continue;
        }

        swap_result(results, i, j);
        swap_page_offset(pages->offset, i, j);
        ram_release_page(rb->idstr, offset);
        j--;
    }

    pages->normal_num = i;
}

#else

static void zero_page_detect_dsa(MultiFDSendParams *p)
{
    exit(1);
}

#endif

void multifd_recv_zero_page_process(MultiFDRecvParams *p)
{
    for (int i = 0; i < p->zero_num; i++) {
        void *page = p->host + p->zero[i];
        if (ramblock_recv_bitmap_test_byte_offset(p->block, p->zero[i])) {
            memset(page, 0, p->page_size);
        } else {
            ramblock_recv_bitmap_set_offset(p->block, p->zero[i]);
        }
    }
}

/**
 * multifd_send_zero_page_detect: Perform zero page detection on all pages.
 *
 * @param p A pointer to the send params.
 */
void multifd_send_zero_page_detect(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = p->pages;

    if (!multifd_zero_page_enabled()) {
        pages->normal_num = pages->num;
        return;
    }

    if (dsa_is_running()) {
        zero_page_detect_dsa(p);
    } else {
        zero_page_detect_cpu(p);
    }
}
