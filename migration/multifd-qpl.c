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
#include "exec/ramblock.h"
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
 * multifd_qpl_prepare_job: prepare a compression or decompression job
 *
 * Prepare a compression or decompression job and configure job attributes
 * including job compression level and flags.
 *
 * @job: pointer to the QplData structure
 * @is_compression: compression or decompression indication
 * @input: pointer to the input data buffer
 * @input_len: the length of the input data
 * @output: pointer to the output data buffer
 * @output_len: the size of the output data buffer
 */
static void multifd_qpl_prepare_job(qpl_job *job, bool is_compression,
                                    uint8_t *input, uint32_t input_len,
                                    uint8_t *output, uint32_t output_len)
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
 * multifd_qpl_build_packet: build a qpl compressed data packet
 *
 * The qpl compressed data packet consists of two parts, one part stores
 * the compressed length of each page, and the other part is the compressed
 * data of each page. The zbuf_hdr stores the compressed length of all pages,
 * and use a separate IOV to store the compressed data of each page.
 *
 * @qpl: pointer to the QplData structure
 * @p: Params for the channel that we are using
 * @idx: The index of the compressed length array
 * @addr: pointer to the compressed data
 * @len: The length of the compressed data
 */
static void multifd_qpl_build_packet(QplData *qpl, MultiFDSendParams *p,
                                     uint32_t idx, uint8_t *addr, uint32_t len)
{
    qpl->zbuf_hdr[idx] = cpu_to_be32(len);
    p->iov[p->iovs_num].iov_base = addr;
    p->iov[p->iovs_num].iov_len = len;
    p->iovs_num++;
    p->next_packet_size += len;
}

/**
 * multifd_qpl_compress_pages: compress normal pages
 *
 * Each normal page will be compressed independently, and the compression jobs
 * will be submitted to the IAA hardware in non-blocking mode, waiting for all
 * jobs to be completed and filling the compressed length and data into the
 * sending IOVs. If IAA device is not available, the software path is used.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_qpl_compress_pages(MultiFDSendParams *p, Error **errp)
{
    qpl_status status;
    QplData *qpl = p->compress_data;
    MultiFDPages_t *pages = p->pages;
    uint8_t *zbuf = qpl->zbuf;
    uint8_t *host = pages->block->host;
    uint32_t job_num = pages->normal_num;
    qpl_job *job = NULL;

    assert(job_num <= qpl->total_job_num);
    /* submit all compression jobs */
    for (int i = 0; i < job_num; i++) {
        job = qpl->job_array[i];
        multifd_qpl_prepare_job(job, true, host + pages->offset[i],
                                p->page_size, zbuf, p->page_size - 1);
        /* if hardware path(IAA) is unavailable, call the software path */
        if (!qpl->iaa_avail) {
            status = qpl_execute_job(job);
            if (status == QPL_STS_OK) {
                multifd_qpl_build_packet(qpl, p, i, zbuf, job->total_out);
            } else if (status == QPL_STS_MORE_OUTPUT_NEEDED) {
                /* compressed length exceeds page size, send page directly */
                multifd_qpl_build_packet(qpl, p, i, host + pages->offset[i],
                                         p->page_size);
            } else {
                error_setg(errp, "multifd %u: qpl_execute_job error %d",
                           p->id, status);
                return -1;
            }
            zbuf += p->page_size;
            continue;
        }
retry:
        status = qpl_submit_job(job);
        if (status == QPL_STS_OK) {
            zbuf += p->page_size;
        } else if (status == QPL_STS_QUEUES_ARE_BUSY_ERR) {
            goto retry;
        } else {
            error_setg(errp, "multifd %u: qpl_submit_job failed with error %d",
                       p->id, status);
            return -1;
        }
    }
    if (!qpl->iaa_avail) {
        goto done;
    }
    /* wait all jobs to complete for hardware(IAA) path */
    for (int i = 0; i < job_num; i++) {
        job = qpl->job_array[i];
        status = qpl_wait_job(job);
        if (status == QPL_STS_OK) {
            multifd_qpl_build_packet(qpl, p, i, qpl->zbuf + (p->page_size * i),
                                     job->total_out);
        } else if (status == QPL_STS_MORE_OUTPUT_NEEDED) {
            /* compressed data length exceeds page size, send page directly */
            multifd_qpl_build_packet(qpl, p, i, host + pages->offset[i],
                                     p->page_size);
        } else {
            error_setg(errp, "multifd %u: qpl_wait_job failed with error %d",
                       p->id, status);
            return -1;
        }
    }
done:
    return 0;
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
    QplData *qpl = p->compress_data;
    uint32_t hdr_size;

    if (!multifd_send_prepare_common(p)) {
        goto out;
    }

    assert(p->pages->normal_num <= qpl->total_job_num);
    hdr_size = p->pages->normal_num * sizeof(uint32_t);
    /* prepare the header that stores the lengths of all compressed data */
    p->iov[1].iov_base = (uint8_t *) qpl->zbuf_hdr;
    p->iov[1].iov_len = hdr_size;
    p->iovs_num++;
    p->next_packet_size += hdr_size;
    if (multifd_qpl_compress_pages(p, errp) != 0) {
        return -1;
    }

out:
    p->flags |= MULTIFD_FLAG_QPL;
    multifd_send_fill_packet(p);
    return 0;
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
 * multifd_qpl_decompress_pages: decompress normal pages
 *
 * Each compressed page will be decompressed independently, and the
 * decompression jobs will be submitted to the IAA hardware in non-blocking
 * mode, waiting for all jobs to be completed and loading the decompressed
 * data into guest memory. If IAA device is not available, the software path
 * is used.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int multifd_qpl_decompress_pages(MultiFDRecvParams *p, Error **errp)
{
    qpl_status status;
    qpl_job *job;
    QplData *qpl = p->compress_data;
    uint32_t job_num = p->normal_num;
    uint32_t off = 0;

    assert(job_num <= qpl->total_job_num);
    /* submit all decompression jobs */
    for (int i = 0; i < job_num; i++) {
        /* if the data size is the same as the page size, load it directly */
        if (qpl->zbuf_hdr[i] == p->page_size) {
            memcpy(p->host + p->normal[i], qpl->zbuf + off, p->page_size);
            off += p->page_size;
            continue;
        }
        job = qpl->job_array[i];
        multifd_qpl_prepare_job(job, false, qpl->zbuf + off, qpl->zbuf_hdr[i],
                                p->host + p->normal[i], p->page_size);
        /* if hardware path(IAA) is unavailable, call the software path */
        if (!qpl->iaa_avail) {
            status = qpl_execute_job(job);
            if (status == QPL_STS_OK) {
                off += qpl->zbuf_hdr[i];
                continue;
            }
            error_setg(errp, "multifd %u: qpl_execute_job failed with error %d",
                       p->id, status);
            return -1;
        }
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
    if (!qpl->iaa_avail) {
        goto done;
    }
    /* wait all jobs to complete for hardware(IAA) path */
    for (int i = 0; i < job_num; i++) {
        if (qpl->zbuf_hdr[i] == p->page_size) {
            continue;
        }
        job = qpl->job_array[i];
        status = qpl_wait_job(job);
        if (status != QPL_STS_OK) {
            error_setg(errp, "multifd %u: qpl_wait_job failed with error %d",
                       p->id, status);
            return -1;
        }
        if (job->total_out != p->page_size) {
            error_setg(errp, "multifd %u: decompressed len %u, expected len %u",
                       p->id, job->total_out, p->page_size);
            return -1;
        }
    }
done:
    return 0;
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
    QplData *qpl = p->compress_data;
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
    multifd_recv_zero_page_process(p);
    if (!p->normal_num) {
        assert(in_size == 0);
        return 0;
    }

    /* read compressed data lengths */
    assert(hdr_len < in_size);
    ret = qio_channel_read_all(p->c, (void *) qpl->zbuf_hdr, hdr_len, errp);
    if (ret != 0) {
        return ret;
    }
    assert(p->normal_num <= qpl->total_job_num);
    for (int i = 0; i < p->normal_num; i++) {
        qpl->zbuf_hdr[i] = be32_to_cpu(qpl->zbuf_hdr[i]);
        data_len += qpl->zbuf_hdr[i];
        assert(qpl->zbuf_hdr[i] <= p->page_size);
    }

    /* read compressed data */
    assert(in_size == hdr_len + data_len);
    ret = qio_channel_read_all(p->c, (void *) qpl->zbuf, data_len, errp);
    if (ret != 0) {
        return ret;
    }

    if (multifd_qpl_decompress_pages(p, errp) != 0) {
        return -1;
    }
    return 0;
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
