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
#include "qemu/module.h"
#include "qapi/error.h"
#include "migration.h"
#include "multifd.h"
#include "qpl/qpl.h"

typedef struct {
    qpl_job **job_array;
    /* the number of allocated jobs */
    uint32_t job_num;
    /* the size of data processed by a qpl job */
    uint32_t data_size;
    /* compressed data buffer */
    uint8_t *zbuf;
    /* the length of compressed data */
    uint32_t *zbuf_hdr;
} QplData;

static void free_zbuf(QplData *qpl)
{
    if (qpl->zbuf != NULL) {
        munmap(qpl->zbuf, qpl->job_num * qpl->data_size);
        qpl->zbuf = NULL;
    }
    if (qpl->zbuf_hdr != NULL) {
        g_free(qpl->zbuf_hdr);
        qpl->zbuf_hdr = NULL;
    }
}

static int alloc_zbuf(QplData *qpl, uint8_t chan_id, Error **errp)
{
    int flags = MAP_PRIVATE | MAP_POPULATE | MAP_ANONYMOUS;
    uint32_t size = qpl->job_num * qpl->data_size;
    uint8_t *buf;

    buf = (uint8_t *) mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (buf == MAP_FAILED) {
        error_setg(errp, "multifd: %u: alloc_zbuf failed, job num %u, size %u",
                   chan_id, qpl->job_num, qpl->data_size);
        return -1;
    }
    qpl->zbuf = buf;
    qpl->zbuf_hdr = g_new0(uint32_t, qpl->job_num);
    return 0;
}

static void free_jobs(QplData *qpl)
{
    for (int i = 0; i < qpl->job_num; i++) {
        qpl_fini_job(qpl->job_array[i]);
        g_free(qpl->job_array[i]);
        qpl->job_array[i] = NULL;
    }
    g_free(qpl->job_array);
    qpl->job_array = NULL;
}

static int alloc_jobs(QplData *qpl, uint8_t chan_id, Error **errp)
{
    qpl_status status;
    uint32_t job_size = 0;
    qpl_job *job = NULL;
    /* always use IAA hardware accelerator */
    qpl_path_t path = qpl_path_hardware;

    status = qpl_get_job_size(path, &job_size);
    if (status != QPL_STS_OK) {
        error_setg(errp, "multifd: %u: qpl_get_job_size failed with error %d",
                   chan_id, status);
        return -1;
    }
    qpl->job_array = g_new0(qpl_job *, qpl->job_num);
    for (int i = 0; i < qpl->job_num; i++) {
        job = g_malloc0(job_size);
        status = qpl_init_job(path, job);
        if (status != QPL_STS_OK) {
            error_setg(errp, "multifd: %u: qpl_init_job failed with error %d",
                       chan_id, status);
            free_jobs(qpl);
            return -1;
        }
        qpl->job_array[i] = job;
    }
    return 0;
}

static int init_qpl(QplData *qpl, uint32_t job_num, uint32_t data_size,
                    uint8_t chan_id, Error **errp)
{
    qpl->job_num = job_num;
    qpl->data_size = data_size;
    if (alloc_zbuf(qpl, chan_id, errp) != 0) {
        return -1;
    }
    if (alloc_jobs(qpl, chan_id, errp) != 0) {
        free_zbuf(qpl);
        return -1;
    }
    return 0;
}

static void deinit_qpl(QplData *qpl)
{
    if (qpl != NULL) {
        free_jobs(qpl);
        free_zbuf(qpl);
        qpl->job_num = 0;
        qpl->data_size = 0;
    }
}

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
    QplData *qpl;

    qpl = g_new0(QplData, 1);
    if (init_qpl(qpl, p->page_count, p->page_size, p->id, errp) != 0) {
        g_free(qpl);
        return -1;
    }
    p->compress_data = qpl;

    assert(p->iov == NULL);
    /*
     * Each page will be compressed independently and sent using an IOV. The
     * additional two IOVs are used to store packet header and compressed data
     * length
     */
    p->iov = g_new0(struct iovec, p->page_count + 2);
    return 0;
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
    QplData *qpl = p->compress_data;

    deinit_qpl(qpl);
    g_free(p->compress_data);
    p->compress_data = NULL;
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
    QplData *qpl;

    qpl = g_new0(QplData, 1);
    if (init_qpl(qpl, p->page_count, p->page_size, p->id, errp) != 0) {
        g_free(qpl);
        return -1;
    }
    p->compress_data = qpl;
    return 0;
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
    QplData *qpl = p->compress_data;

    deinit_qpl(qpl);
    g_free(p->compress_data);
    p->compress_data = NULL;
}

/**
 * qpl_recv: read the data from the channel into actual pages
 *
 * Read the compressed buffer, and uncompress it into the actual
 * pages.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int qpl_recv(MultiFDRecvParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

static MultiFDMethods multifd_qpl_ops = {
    .send_setup = qpl_send_setup,
    .send_cleanup = qpl_send_cleanup,
    .send_prepare = qpl_send_prepare,
    .recv_setup = qpl_recv_setup,
    .recv_cleanup = qpl_recv_cleanup,
    .recv = qpl_recv,
};

static void multifd_qpl_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_QPL, &multifd_qpl_ops);
}

migration_init(multifd_qpl_register);
