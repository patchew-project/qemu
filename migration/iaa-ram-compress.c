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
