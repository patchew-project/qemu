/*
 * QMP interface for background jobs
 *
 * Copyright (c) 2011 IBM Corp.
 * Copyright (c) 2012, 2018 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/job.h"
#include "qapi/qapi-commands-job.h"
#include "qapi/error.h"
#include "trace/trace-root.h"

/* Get a job using its ID and acquire its job_lock */
static Job *find_job(const char *id, Error **errp)
{
    Job *job;

    job_lock();

    job = job_get(id);
    if (!job) {
        error_setg(errp, "Job not found");
        job_unlock();
        return NULL;
    }

    return job;
}

void qmp_job_cancel(const char *id, Error **errp)
{
    Job *job = find_job(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_cancel(job);
    job_user_cancel(job, true, errp);
    job_unlock();
}

void qmp_job_pause(const char *id, Error **errp)
{
    Job *job = find_job(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_pause(job);
    job_user_pause(job, errp);
    job_unlock();
}

void qmp_job_resume(const char *id, Error **errp)
{
    Job *job = find_job(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_resume(job);
    job_user_resume(job, errp);
    job_unlock();
}

void qmp_job_complete(const char *id, Error **errp)
{
    Job *job = find_job(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_complete(job);
    job_complete(job, errp);
    job_unlock();
}

void qmp_job_finalize(const char *id, Error **errp)
{
    Job *job = find_job(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_finalize(job);
    job_ref(job);
    job_finalize(job, errp);

    job_unref(job);
    job_unlock();
}

void qmp_job_dismiss(const char *id, Error **errp)
{
    Job *job = find_job(id, errp);

    if (!job) {
        return;
    }

    trace_qmp_job_dismiss(job);
    job_dismiss(&job, errp);
    job_unlock();
}

/* Called with job_mutex held. */
static JobInfo *job_query_single(Job *job, Error **errp)
{
    JobInfo *info;
    uint64_t progress_current;
    uint64_t progress_total;
    Error *job_err;

    assert(!job_is_internal(job));
    progress_get_snapshot(&job->progress, &progress_current,
                          &progress_total);
    job_err = job_get_err(job);

    info = g_new(JobInfo, 1);
    *info = (JobInfo) {
        .id                 = g_strdup(job->id),
        .type               = job_type(job),
        .status             = job_get_status(job),
        .current_progress   = progress_current,
        .total_progress     = progress_total,
        .has_error          = !!job_err,
        .error              = job_err ? \
                              g_strdup(error_get_pretty(job_err)) : NULL,
    };

    return info;
}

JobInfoList *qmp_query_jobs(Error **errp)
{
    JobInfoList *head = NULL, **tail = &head;
    Job *job;

    for (job = job_next(NULL); job; job = job_next(job)) {
        JobInfo *value;

        if (job_is_internal(job)) {
            continue;
        }

        job_lock();
        value = job_query_single(job, errp);
        job_unlock();

        if (!value) {
            qapi_free_JobInfoList(head);
            return NULL;
        }
        QAPI_LIST_APPEND(tail, value);
    }

    return head;
}
