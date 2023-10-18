/*
 * QEMU IAA compression support
 *
 * Copyright (c) 2023 Intel Corporation
 *  Written by:
 *  Yuan Liu<yuan1.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"

#include "qemu/error-report.h"
#include "migration.h"
#include "options.h"
#include "io/channel-null.h"
#include "exec/target_page.h"
#include "exec/ramblock.h"
#include "iaa-ram-compress.h"
#include "qpl/qpl.h"

/* The IAA work queue maximum depth */
#define IAA_JOB_NUM (512)

typedef struct {
    CompressResult result;
    ram_addr_t offset; /* The offset of the compressed page in the block */
    RAMBlock *block; /* The block of the compressed page */
} iaa_comp_param;

typedef struct {
    uint8_t *host; /* Target address for decompression page */
} iaa_decomp_param;

typedef struct IaaJob {
    QSIMPLEQ_ENTRY(IaaJob) entry;
    bool is_compression;
    uint32_t in_len;
    uint32_t out_len;
    uint8_t *in_buf;
    uint8_t *out_buf;
    qpl_job *qpl; /* It is used to submit (de)compression work to IAA */
    union {
        iaa_comp_param comp;
        iaa_decomp_param decomp;
    } param;
} IaaJob;

typedef struct IaaJobPool {
    uint32_t pos;
    uint32_t cnt;
    IaaJob *jobs[IAA_JOB_NUM];
    uint8_t *job_in_buf; /* The IAA device input buffers for all IAA jobs */
    uint8_t *job_out_buf; /* The IAA device output buffers for all IAA jobs */
    size_t buf_size;
} IaaJobPool;

static IaaJobPool iaa_job_pool;
/* This is used to record jobs that have been submitted but not yet completed */
static QSIMPLEQ_HEAD(, IaaJob) polling_queue =
                                   QSIMPLEQ_HEAD_INITIALIZER(polling_queue);

static IaaJob *get_job(send_iaa_data send_page)
{
    IaaJob *job;

retry:
    /* Wait for a job to complete when there is no available job */
    if (iaa_job_pool.cnt == IAA_JOB_NUM) {
        flush_iaa_jobs(false, send_page);
        goto retry;
    }
    job = iaa_job_pool.jobs[iaa_job_pool.pos];
    iaa_job_pool.pos++;
    iaa_job_pool.cnt++;
    if (iaa_job_pool.pos == IAA_JOB_NUM) {
        iaa_job_pool.pos = 0;
    }
    return job;
}

static void put_job(IaaJob *job)
{
    assert(iaa_job_pool.cnt > 0);
    iaa_job_pool.cnt--;
}

void iaa_compress_deinit(void)
{
    for (int i = 0; i < IAA_JOB_NUM; i++) {
        if (iaa_job_pool.jobs[i]) {
            if (iaa_job_pool.jobs[i]->qpl) {
                qpl_fini_job(iaa_job_pool.jobs[i]->qpl);
                g_free(iaa_job_pool.jobs[i]->qpl);
            }
            g_free(iaa_job_pool.jobs[i]);
        }
    }
    if (iaa_job_pool.job_in_buf) {
        munmap(iaa_job_pool.job_in_buf, iaa_job_pool.buf_size);
        iaa_job_pool.job_in_buf = NULL;
    }
    if (iaa_job_pool.job_out_buf) {
        munmap(iaa_job_pool.job_out_buf, iaa_job_pool.buf_size);
        iaa_job_pool.job_out_buf = NULL;
    }
}

int iaa_compress_init(bool is_decompression)
{
    qpl_status status;
    IaaJob *job = NULL;
    uint32_t qpl_hw_size = 0;
    int flags = MAP_PRIVATE | MAP_POPULATE | MAP_ANONYMOUS;
    size_t buf_size = IAA_JOB_NUM * qemu_target_page_size();

    QSIMPLEQ_INIT(&polling_queue);
    memset(&iaa_job_pool, 0, sizeof(IaaJobPool));
    iaa_job_pool.buf_size = buf_size;
    iaa_job_pool.job_out_buf = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                                    flags, -1, 0);
    if (iaa_job_pool.job_out_buf == MAP_FAILED) {
        error_report("Failed to allocate iaa output buffer, error %s",
                     strerror(errno));
        return -1;
    }
    /*
     * There is no need to allocate an input buffer for the compression
     * function, the IAA hardware can directly access the virtual machine
     * memory through the host address through Share Virtual Memory(SVM)
     */
    if (is_decompression) {
        iaa_job_pool.job_in_buf = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                                       flags, -1, 0);
        if (iaa_job_pool.job_in_buf == MAP_FAILED) {
            error_report("Failed to allocate iaa input buffer, error %s",
                         strerror(errno));
            goto init_err;
        }
    }
    status = qpl_get_job_size(qpl_path_hardware, &qpl_hw_size);
    if (status != QPL_STS_OK) {
        error_report("Failed to initialize iaa hardware, error %d", status);
        goto init_err;
    }
    for (int i = 0; i < IAA_JOB_NUM; i++) {
        size_t buf_offset = qemu_target_page_size() * i;
        job = g_try_malloc0(sizeof(IaaJob));
        if (!job) {
            error_report("Failed to allocate iaa job memory, error %s",
                         strerror(errno));
            goto init_err;
        }
        iaa_job_pool.jobs[i] = job;
        job->qpl = g_try_malloc0(qpl_hw_size);
        if (!job->qpl) {
            error_report("Failed to allocate iaa qpl memory, error %s",
                         strerror(errno));
            goto init_err;
        }
        if (is_decompression) {
            job->in_buf = iaa_job_pool.job_in_buf + buf_offset;
        }
        job->out_buf = iaa_job_pool.job_out_buf + buf_offset;
        status = qpl_init_job(qpl_path_hardware, job->qpl);
        if (status != QPL_STS_OK) {
            error_report("Failed to initialize iaa qpl, error %d", status);
            goto init_err;
        }
    }
    return 0;
init_err:
    iaa_compress_deinit();
    return -1;
}

static void process_completed_job(IaaJob *job, send_iaa_data send_page)
{
    if (job->is_compression) {
        send_page(job->param.comp.block, job->param.comp.offset,
                  job->out_buf, job->out_len, job->param.comp.result);
    } else {
        assert(job->out_len == qemu_target_page_size());
        memcpy(job->param.decomp.host, job->out_buf, job->out_len);
    }
    put_job(job);
}

static qpl_status check_job_status(IaaJob *job, bool block)
{
    qpl_status status;
    qpl_job *qpl = job->qpl;

    status = block ? qpl_wait_job(qpl) : qpl_check_job(qpl);
    if (status == QPL_STS_OK) {
        job->out_len = qpl->total_out;
        if (job->is_compression) {
            job->param.comp.result = RES_COMPRESS;
            /* if no compression benefit, send a normal page for migration */
            if (job->out_len == qemu_target_page_size()) {
                iaa_comp_param *param = &(job->param.comp);
                memcpy(job->out_buf, (param->block->host + param->offset),
                       job->out_len);
                job->param.comp.result = RES_NONE;
            }
        }
    } else if (status == QPL_STS_MORE_OUTPUT_NEEDED) {
        if (job->is_compression) {
            /*
             * if the compressed data is larger than the original data, send a
             * normal page for migration, in this case, IAA has copied the
             * original data to job->out_buf automatically.
             */
            job->out_len = qemu_target_page_size();
            job->param.comp.result = RES_NONE;
            status = QPL_STS_OK;
        }
    }
    return status;
}

static void check_polling_jobs(send_iaa_data send_page)
{
    IaaJob *job, *job_next;
    qpl_status status;

    QSIMPLEQ_FOREACH_SAFE(job, &polling_queue, entry, job_next) {
        status = check_job_status(job, false);
        if (status == QPL_STS_OK) { /* job has done */
            process_completed_job(job, send_page);
            QSIMPLEQ_REMOVE_HEAD(&polling_queue, entry);
        } else if (status == QPL_STS_BEING_PROCESSED) { /* job is running */
            break;
        } else {
            abort();
        }
    }
}

static int submit_new_job(IaaJob *job)
{
    qpl_status status;
    qpl_job *qpl = job->qpl;

    qpl->op = job->is_compression ? qpl_op_compress : qpl_op_decompress;
    qpl->next_in_ptr = job->in_buf;
    qpl->next_out_ptr = job->out_buf;
    qpl->available_in = job->in_len;
    qpl->available_out = qemu_target_page_size(); /* outbuf maximum size */
    qpl->flags = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_OMIT_VERIFY;
    qpl->level = 1; /* only level 1 compression is supported */

    do {
        status = qpl_submit_job(qpl);
    } while (status == QPL_STS_QUEUES_ARE_BUSY_ERR);

    if (status != QPL_STS_OK) {
        error_report("Failed to submit iaa job, error %d", status);
        return -1;
    }
    QSIMPLEQ_INSERT_TAIL(&polling_queue, job, entry);
    return 0;
}

int flush_iaa_jobs(bool flush_all_jobs, send_iaa_data send_page)
{
    IaaJob *job, *job_next;

    QSIMPLEQ_FOREACH_SAFE(job, &polling_queue, entry, job_next) {
        if (check_job_status(job, true) != QPL_STS_OK) {
            return -1;
        }
        process_completed_job(job, send_page);
        QSIMPLEQ_REMOVE_HEAD(&polling_queue, entry);
        if (!flush_all_jobs) {
            break;
        }
    }
    return 0;
}

int compress_page_with_iaa(RAMBlock *block, ram_addr_t offset,
                           send_iaa_data send_page)
{
    IaaJob *job;

    if (iaa_job_pool.cnt != 0) {
        check_polling_jobs(send_page);
    }
    if (buffer_is_zero(block->host + offset, qemu_target_page_size())) {
        send_page(block, offset, NULL, 0, RES_ZEROPAGE);
        return 1;
    }
    job = get_job(send_page);
    job->is_compression = true;
    job->in_buf = block->host + offset;
    job->in_len = qemu_target_page_size();
    job->param.comp.offset = offset;
    job->param.comp.block = block;
    return (submit_new_job(job) == 0 ? 1 : 0);
}

int decompress_data_with_iaa(QEMUFile *f, void *host, int len)
{
    IaaJob *job;

    if (iaa_job_pool.cnt != 0) {
        check_polling_jobs(NULL);
    }
    job = get_job(NULL);
    job->is_compression = false;
    qemu_get_buffer(f, job->in_buf, len);
    job->in_len = len;
    job->param.decomp.host = host;
    return submit_new_job(job);
}
