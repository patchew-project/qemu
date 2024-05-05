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
    uint32_t total_job_num;
    /* compressed data buffer */
    uint8_t *zbuf;
    /* the length of compressed data */
    uint32_t *zbuf_hdr;
    /* the status of IAA device */
    bool iaa_avail;
} QplData;

/**
 * check_iaa_avail: check if IAA device is available
 *
 * If the system does not have an IAA device, the IAA device is
 * not enabled or the IAA work queue is not configured as a shared
 * mode, the QPL hardware path initialization will fail.
 *
 * Returns true if IAA device is available, otherwise false.
 */
static bool check_iaa_avail(void)
{
    qpl_job *job = NULL;
    uint32_t job_size = 0;
    qpl_path_t path = qpl_path_hardware;

    if (qpl_get_job_size(path, &job_size) != QPL_STS_OK) {
        return false;
    }
    job = g_malloc0(job_size);
    if (qpl_init_job(path, job) != QPL_STS_OK) {
        g_free(job);
        return false;
    }
    g_free(job);
    return true;
}

/**
 * multifd_qpl_free_jobs: cleanup jobs
 *
 * Free all job resources.
 *
 * @qpl: pointer to the QplData structure
 */
static void multifd_qpl_free_jobs(QplData *qpl)
{
    assert(qpl != NULL);
    for (int i = 0; i < qpl->total_job_num; i++) {
        qpl_fini_job(qpl->job_array[i]);
        g_free(qpl->job_array[i]);
        qpl->job_array[i] = NULL;
    }
    g_free(qpl->job_array);
    qpl->job_array = NULL;
}

/**
 * multifd_qpl_init_jobs: initialize jobs
 *
 * Initialize all jobs
 *
 * @qpl: pointer to the QplData structure
 * @chan_id: multifd channel number
 * @errp: pointer to an error
 */
static int multifd_qpl_init_jobs(QplData *qpl, uint8_t chan_id, Error **errp)
{
    qpl_path_t path;
    qpl_status status;
    uint32_t job_size = 0;
    qpl_job *job = NULL;

    path = qpl->iaa_avail ? qpl_path_hardware : qpl_path_software;
    status = qpl_get_job_size(path, &job_size);
    if (status != QPL_STS_OK) {
        error_setg(errp, "multifd: %u: qpl_get_job_size failed with error %d",
                   chan_id, status);
        return -1;
    }
    qpl->job_array = g_new0(qpl_job *, qpl->total_job_num);
    for (int i = 0; i < qpl->total_job_num; i++) {
        job = g_malloc0(job_size);
        status = qpl_init_job(path, job);
        if (status != QPL_STS_OK) {
            error_setg(errp, "multifd: %u: qpl_init_job failed with error %d",
                       chan_id, status);
            multifd_qpl_free_jobs(qpl);
            return -1;
        }
        qpl->job_array[i] = job;
    }
    return 0;
}

/**
 * multifd_qpl_init: initialize QplData structure
 *
 * Allocate and initialize a QplData structure
 *
 * Returns QplData pointer for success or NULL for error
 *
 * @job_num: pointer to the QplData structure
 * @job_size: the buffer size of the job
 * @chan_id: multifd channel number
 * @errp: pointer to an error
 */
static QplData *multifd_qpl_init(uint32_t job_num, uint32_t job_size,
                                 uint8_t chan_id, Error **errp)
{
    QplData *qpl;

    qpl = g_new0(QplData, 1);
    qpl->total_job_num = job_num;
    qpl->iaa_avail = check_iaa_avail();
    if (multifd_qpl_init_jobs(qpl, chan_id, errp) != 0) {
        g_free(qpl);
        return NULL;
    }
    qpl->zbuf = g_malloc0(job_size * job_num);
    qpl->zbuf_hdr = g_new0(uint32_t, job_num);
    return qpl;
}

/**
 * multifd_qpl_deinit: cleanup QplData structure
 *
 * Free jobs, comprssed buffers and QplData structure
 *
 * @qpl: pointer to the QplData structure
 */
static void multifd_qpl_deinit(QplData *qpl)
{
    if (qpl != NULL) {
        multifd_qpl_free_jobs(qpl);
        g_free(qpl->zbuf_hdr);
        g_free(qpl->zbuf);
        g_free(qpl);
    }
}

/**
 * multifd_qpl_send_setup: setup send side
 *
 * Setup each channel with QPL compression.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_qpl_send_setup(MultiFDSendParams *p, Error **errp)
{
    QplData *qpl;

    qpl = multifd_qpl_init(p->page_count, p->page_size, p->id, errp);
    if (!qpl) {
        return -1;
    }
    p->compress_data = qpl;

    /*
     * Each page will be compressed independently and sent using an IOV. The
     * additional two IOVs are used to store packet header and compressed data
     * length
     */
    p->iov = g_new0(struct iovec, p->page_count + 2);
    return 0;
}

/**
 * multifd_qpl_send_cleanup: cleanup send side
 *
 * Close the channel and return memory.
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static void multifd_qpl_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    multifd_qpl_deinit(p->compress_data);
    p->compress_data = NULL;
    g_free(p->iov);
    p->iov = NULL;
}

/**
 * multifd_qpl_send_prepare: prepare data to be able to send
 *
 * Create a compressed buffer with all the pages that we are going to
 * send.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_qpl_send_prepare(MultiFDSendParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

/**
 * multifd_qpl_recv_setup: setup receive side
 *
 * Create the compressed channel and buffer.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_qpl_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    QplData *qpl;

    qpl = multifd_qpl_init(p->page_count, p->page_size, p->id, errp);
    if (!qpl) {
        return -1;
    }
    p->compress_data = qpl;
    return 0;
}

/**
 * multifd_qpl_recv_cleanup: setup receive side
 *
 * Close the channel and return memory.
 *
 * @p: Params for the channel that we are using
 */
static void multifd_qpl_recv_cleanup(MultiFDRecvParams *p)
{
    multifd_qpl_deinit(p->compress_data);
    p->compress_data = NULL;
}

/**
 * multifd_qpl_recv: read the data from the channel into actual pages
 *
 * Read the compressed buffer, and uncompress it into the actual
 * pages.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_qpl_recv(MultiFDRecvParams *p, Error **errp)
{
    /* Implement in next patch */
    return -1;
}

static MultiFDMethods multifd_qpl_ops = {
    .send_setup = multifd_qpl_send_setup,
    .send_cleanup = multifd_qpl_send_cleanup,
    .send_prepare = multifd_qpl_send_prepare,
    .recv_setup = multifd_qpl_recv_setup,
    .recv_cleanup = multifd_qpl_recv_cleanup,
    .recv = multifd_qpl_recv,
};

static void multifd_qpl_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_QPL, &multifd_qpl_ops);
}

migration_init(multifd_qpl_register);
