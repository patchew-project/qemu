/*
 * Multifd qpl compression accelerator implementation
 *
 * Copyright (c) 2023 Intel Corporation
 *
 * Authors:
 *  Yuan Liu<yuan1.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/rcu.h"
#include "exec/ramblock.h"
#include "exec/target_page.h"
#include "qapi/error.h"
#include "migration.h"
#include "trace.h"
#include "options.h"
#include "multifd.h"
#include "qpl/qpl.h"

struct qpl_data {
    qpl_job **job_array;
    /* the number of allocated jobs */
    uint32_t job_num;
    /* the size of data processed by a qpl job */
    uint32_t data_size;
    /* compressed data buffer */
    uint8_t *zbuf;
    /* the length of compressed data */
    uint32_t *zbuf_hdr;
};

/**
 * qpl_send_setup: setup send side
 *
 * Setup each channel with QPL compression.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int qpl_send_setup(MultiFDSendParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

/**
 * qpl_send_cleanup: cleanup send side
 *
 * Close the channel and return memory.
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static void qpl_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    /* Implement in next patch */
}

/**
 * qpl_send_prepare: prepare data to be able to send
 *
 * Create a compressed buffer with all the pages that we are going to
 * send.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int qpl_send_prepare(MultiFDSendParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

/**
 * qpl_recv_setup: setup receive side
 *
 * Create the compressed channel and buffer.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int qpl_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

/**
 * qpl_recv_cleanup: setup receive side
 *
 * Close the channel and return memory.
 *
 * @p: Params for the channel that we are using
 */
static void qpl_recv_cleanup(MultiFDRecvParams *p)
{
    /* Implement in next patch */
}

/**
 * qpl_recv_pages: read the data from the channel into actual pages
 *
 * Read the compressed buffer, and uncompress it into the actual
 * pages.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int qpl_recv_pages(MultiFDRecvParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

/**
 * qpl_get_iov_count: get the count of IOVs
 *
 * For QPL compression, in addition to requesting the same number of IOVs
 * as the page, it also requires an additional IOV to store all compressed
 * data lengths.
 *
 * Returns the count of the IOVs
 *
 * @page_count: Indicate the maximum count of pages processed by multifd
 */
static uint32_t qpl_get_iov_count(uint32_t page_count)
{
    return page_count + 1;
}

static MultiFDMethods multifd_qpl_ops = {
    .send_setup = qpl_send_setup,
    .send_cleanup = qpl_send_cleanup,
    .send_prepare = qpl_send_prepare,
    .recv_setup = qpl_recv_setup,
    .recv_cleanup = qpl_recv_cleanup,
    .recv_pages = qpl_recv_pages,
    .get_iov_count = qpl_get_iov_count
};

static void multifd_qpl_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_QPL, &multifd_qpl_ops);
}

migration_init(multifd_qpl_register);
