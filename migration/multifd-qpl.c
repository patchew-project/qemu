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

static void free_zbuf(struct qpl_data *qpl)
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

static int alloc_zbuf(struct qpl_data *qpl, uint8_t chan_id, Error **errp)
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

static void free_jobs(struct qpl_data *qpl)
{
    for (int i = 0; i < qpl->job_num; i++) {
        qpl_fini_job(qpl->job_array[i]);
        g_free(qpl->job_array[i]);
        qpl->job_array[i] = NULL;
    }
    g_free(qpl->job_array);
    qpl->job_array = NULL;
}

static int alloc_jobs(struct qpl_data *qpl, uint8_t chan_id, Error **errp)
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

static int init_qpl(struct qpl_data *qpl, uint32_t job_num, uint32_t data_size,
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

static void deinit_qpl(struct qpl_data *qpl)
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
    struct qpl_data *qpl;

    qpl = g_new0(struct qpl_data, 1);
    if (init_qpl(qpl, p->page_count, p->page_size, p->id, errp) != 0) {
        g_free(qpl);
        return -1;
    }
    p->data = qpl;
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
    struct qpl_data *qpl = p->data;

    deinit_qpl(qpl);
    g_free(p->data);
    p->data = NULL;
}

static inline void prepare_job(qpl_job *job, uint8_t *input, uint32_t input_len,
                               uint8_t *output, uint32_t output_len,
                               bool is_compression)
{
    job->op = is_compression ? qpl_op_compress : qpl_op_decompress;
    job->next_in_ptr = input;
    job->next_out_ptr = output;
    job->available_in = input_len;
    job->available_out = output_len;
    job->flags = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
    /* only supports one compression level */
    job->level = 1;
}

/**
 * set_raw_data_hdr: set the length of raw data
 *
 * If the length of the compressed output data is greater than or equal to
 * the page size, then set the compressed data length to the data size and
 * send raw data directly.
 *
 * @qpl: pointer to the qpl_data structure
 * @index: the index of the compression job header
 */
static inline void set_raw_data_hdr(struct qpl_data *qpl, uint32_t index)
{
    assert(index < qpl->job_num);
    qpl->zbuf_hdr[index] = cpu_to_be32(qpl->data_size);
}

/**
 * is_raw_data: check if the data is raw data
 *
 * The raw data length is always equal to data size, which is the
 * size of one page.
 *
 * Returns true if the data is raw data, otherwise false
 *
 * @qpl: pointer to the qpl_data structure
 * @index: the index of the decompressed job header
 */
static inline bool is_raw_data(struct qpl_data *qpl, uint32_t index)
{
    assert(index < qpl->job_num);
    return qpl->zbuf_hdr[index] == qpl->data_size;
}

static int run_comp_jobs(MultiFDSendParams *p, Error **errp)
{
    qpl_status status;
    struct qpl_data *qpl = p->data;
    MultiFDPages_t *pages = p->pages;
    uint32_t job_num = pages->num;
    qpl_job *job = NULL;
    uint32_t off = 0;

    assert(job_num <= qpl->job_num);
    /* submit all compression jobs */
    for (int i = 0; i < job_num; i++) {
        job = qpl->job_array[i];
        /* the compressed data size should be less than one page */
        prepare_job(job, pages->block->host + pages->offset[i], qpl->data_size,
                    qpl->zbuf + off, qpl->data_size - 1, true);
retry:
        status = qpl_submit_job(job);
        if (status == QPL_STS_OK) {
            off += qpl->data_size;
        } else if (status == QPL_STS_QUEUES_ARE_BUSY_ERR) {
            goto retry;
        } else {
            error_setg(errp, "multifd %u: qpl_submit_job failed with error %d",
                       p->id, status);
            return -1;
        }
    }

    /* wait all jobs to complete */
    for (int i = 0; i < job_num; i++) {
        job = qpl->job_array[i];
        status = qpl_wait_job(job);
        if (status == QPL_STS_OK) {
            qpl->zbuf_hdr[i] = cpu_to_be32(job->total_out);
            p->iov[p->iovs_num].iov_len = job->total_out;
            p->iov[p->iovs_num].iov_base = qpl->zbuf + (qpl->data_size * i);
            p->next_packet_size += job->total_out;
        } else if (status == QPL_STS_MORE_OUTPUT_NEEDED) {
            /*
             * the compression job does not fail, the output data
             * size is larger than the provided memory size. In this
             * case, raw data is sent directly to the destination.
             */
            set_raw_data_hdr(qpl, i);
            p->iov[p->iovs_num].iov_len = qpl->data_size;
            p->iov[p->iovs_num].iov_base = pages->block->host +
                                           pages->offset[i];
            p->next_packet_size += qpl->data_size;
        } else {
            error_setg(errp, "multifd %u: qpl_wait_job failed with error %d",
                       p->id, status);
            return -1;
        }
        p->iovs_num++;
    }
    return 0;
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
    struct qpl_data *qpl = p->data;
    uint32_t hdr_size = p->pages->num * sizeof(uint32_t);

    multifd_send_prepare_header(p);

    assert(p->pages->num <= qpl->job_num);
    /* prepare the header that stores the lengths of all compressed data */
    p->iov[1].iov_base = (uint8_t *) qpl->zbuf_hdr;
    p->iov[1].iov_len = hdr_size;
    p->iovs_num++;
    p->next_packet_size += hdr_size;
    p->flags |= MULTIFD_FLAG_QPL;

    if (run_comp_jobs(p, errp) != 0) {
        return -1;
    }

    multifd_send_fill_packet(p);
    return 0;
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
    struct qpl_data *qpl;

    qpl = g_new0(struct qpl_data, 1);
    if (init_qpl(qpl, p->page_count, p->page_size, p->id, errp) != 0) {
        g_free(qpl);
        return -1;
    }
    p->data = qpl;
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
    struct qpl_data *qpl = p->data;

    deinit_qpl(qpl);
    g_free(p->data);
    p->data = NULL;
}

static int run_decomp_jobs(MultiFDRecvParams *p, Error **errp)
{
    qpl_status status;
    qpl_job *job;
    struct qpl_data *qpl = p->data;
    uint32_t off = 0;
    uint32_t job_num = p->normal_num;

    assert(job_num <= qpl->job_num);
    /* submit all decompression jobs */
    for (int i = 0; i < job_num; i++) {
        /* for the raw data, load it directly */
        if (is_raw_data(qpl, i)) {
            memcpy(p->host + p->normal[i], qpl->zbuf + off, qpl->data_size);
            off += qpl->data_size;
            continue;
        }
        job = qpl->job_array[i];
        prepare_job(job, qpl->zbuf + off, qpl->zbuf_hdr[i],
                    p->host + p->normal[i], qpl->data_size, false);
retry:
        status = qpl_submit_job(job);
        if (status == QPL_STS_OK) {
            off += qpl->zbuf_hdr[i];
        } else if (status == QPL_STS_QUEUES_ARE_BUSY_ERR) {
            goto retry;
        } else {
            error_setg(errp, "multifd %u: qpl_submit_job failed with error %d",
                       p->id, status);
            return -1;
        }
    }

    /* wait all jobs to complete */
    for (int i = 0; i < job_num; i++) {
        if (is_raw_data(qpl, i)) {
            continue;
        }
        job = qpl->job_array[i];
        status = qpl_wait_job(job);
        if (status != QPL_STS_OK) {
            error_setg(errp, "multifd %u: qpl_wait_job failed with error %d",
                       p->id, status);
            return -1;
        }
        if (job->total_out != qpl->data_size) {
            error_setg(errp, "multifd %u: decompressed len %u, expected len %u",
                       p->id, job->total_out, qpl->data_size);
            return -1;
        }
    }
    return 0;
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
    struct qpl_data *qpl = p->data;
    uint32_t in_size = p->next_packet_size;
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;
    uint32_t hdr_len = p->normal_num * sizeof(uint32_t);
    uint32_t data_len = 0;
    int ret;

    if (flags != MULTIFD_FLAG_QPL) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_QPL);
        return -1;
    }
    /* read comprssed data lengths */
    assert(hdr_len < in_size);
    ret = qio_channel_read_all(p->c, (void *) qpl->zbuf_hdr, hdr_len, errp);
    if (ret != 0) {
        return ret;
    }
    assert(p->normal_num <= qpl->job_num);
    for (int i = 0; i < p->normal_num; i++) {
        qpl->zbuf_hdr[i] = be32_to_cpu(qpl->zbuf_hdr[i]);
        data_len += qpl->zbuf_hdr[i];
        assert(qpl->zbuf_hdr[i] <= qpl->data_size);
    }

    /* read comprssed data */
    assert(in_size == hdr_len + data_len);
    ret = qio_channel_read_all(p->c, (void *) qpl->zbuf, data_len, errp);
    if (ret != 0) {
        return ret;
    }

    if (run_decomp_jobs(p, errp) != 0) {
        return -1;
    }
    return 0;
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
