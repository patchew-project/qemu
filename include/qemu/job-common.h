/*
 * Declarations for background jobs
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

#ifndef JOB_COMMON_H
#define JOB_COMMON_H

#include "qapi/qapi-types-job.h"
#include "qemu/queue.h"
#include "qemu/progress_meter.h"
#include "qemu/coroutine.h"
#include "block/aio.h"

typedef struct JobDriver JobDriver;
typedef struct JobTxn JobTxn;


/**
 * Long-running operation.
 */
typedef struct Job {

    /* Fields set at initialization (job_create), and never modified */

    /** The ID of the job. May be NULL for internal jobs. */
    char *id;

    /**
     * The type of this job.
     * All callbacks are called with job_mutex *not* held.
     */
    const JobDriver *driver;

    /** AioContext to run the job coroutine in */
    AioContext *aio_context;

    /**
     * The coroutine that executes the job.  If not NULL, it is reentered when
     * busy is false and the job is cancelled.
     * Initialized in job_start()
     */
    Coroutine *co;

    /** True if this job should automatically finalize itself */
    bool auto_finalize;

    /** True if this job should automatically dismiss itself */
    bool auto_dismiss;

    /** The completion function that will be called when the job completes.  */
    BlockCompletionFunc *cb;

    /** The opaque value that is passed to the completion function.  */
    void *opaque;

    /* ProgressMeter API is thread-safe */
    ProgressMeter progress;


    /** Protected by job_mutex */

    /** Reference count of the block job */
    int refcnt;

    /** Current state; See @JobStatus for details. */
    JobStatus status;

    /**
     * Timer that is used by @job_sleep_ns. Accessed under job_mutex (in
     * job.c).
     */
    QEMUTimer sleep_timer;

    /**
     * Counter for pause request. If non-zero, the block job is either paused,
     * or if busy == true will pause itself as soon as possible.
     */
    int pause_count;

    /**
     * Set to false by the job while the coroutine has yielded and may be
     * re-entered by job_enter(). There may still be I/O or event loop activity
     * pending. Accessed under job_mutex.
     *
     * When the job is deferred to the main loop, busy is true as long as the
     * bottom half is still pending.
     */
    bool busy;

    /**
     * Set to true by the job while it is in a quiescent state, where
     * no I/O or event loop activity is pending.
     */
    bool paused;

    /**
     * Set to true if the job is paused by user.  Can be unpaused with the
     * block-job-resume QMP command.
     */
    bool user_paused;

    /**
     * Set to true if the job should cancel itself.  The flag must
     * always be tested just before toggling the busy flag from false
     * to true.  After a job has been cancelled, it should only yield
     * if #aio_poll will ("sooner or later") reenter the coroutine.
     */
    bool cancelled;

    /**
     * Set to true if the job should abort immediately without waiting
     * for data to be in sync.
     */
    bool force_cancel;

    /** Set to true when the job has deferred work to the main loop. */
    bool deferred_to_main_loop;

    /**
     * Return code from @run and/or @prepare callback(s).
     * Not final until the job has reached the CONCLUDED status.
     * 0 on success, -errno on failure.
     */
    int ret;

    /**
     * Error object for a failed job.
     * If job->ret is nonzero and an error object was not set, it will be set
     * to strerror(-job->ret) during job_completed.
     */
    Error *err;

    /** Notifiers called when a cancelled job is finalised */
    NotifierList on_finalize_cancelled;

    /** Notifiers called when a successfully completed job is finalised */
    NotifierList on_finalize_completed;

    /** Notifiers called when the job transitions to PENDING */
    NotifierList on_pending;

    /** Notifiers called when the job transitions to READY */
    NotifierList on_ready;

    /** Notifiers called when the job coroutine yields or terminates */
    NotifierList on_idle;

    /** Element of the list of jobs */
    QLIST_ENTRY(Job) job_list;

    /** Transaction this job is part of */
    JobTxn *txn;

    /** Element of the list of jobs in a job transaction */
    QLIST_ENTRY(Job) txn_list;
} Job;

/**
 * Callbacks and other information about a Job driver.
 * All callbacks are invoked with job_mutex *not* held.
 */
struct JobDriver {

    /* Fields initialized in struct definition and never changed. */

    /** Derived Job struct size */
    size_t instance_size;

    /** Enum describing the operation */
    JobType job_type;

    /*
     * Functions run without regard to the BQL and may run in any
     * arbitrary thread. These functions do not need to be thread-safe
     * because the caller ensures that are invoked from one thread at time.
     */

    /**
     * Mandatory: Entrypoint for the Coroutine.
     *
     * This callback will be invoked when moving from CREATED to RUNNING.
     *
     * If this callback returns nonzero, the job transaction it is part of is
     * aborted. If it returns zero, the job moves into the WAITING state. If it
     * is the last job to complete in its transaction, all jobs in the
     * transaction move from WAITING to PENDING.
     */
    int coroutine_fn (*run)(Job *job, Error **errp);

    /**
     * If the callback is not NULL, it will be invoked when the job transitions
     * into the paused state.  Paused jobs must not perform any asynchronous
     * I/O or event loop activity.  This callback is used to quiesce jobs.
     */
    void coroutine_fn (*pause)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked when the job transitions
     * out of the paused state.  Any asynchronous I/O or event loop activity
     * should be restarted from this callback.
     */
    void coroutine_fn (*resume)(Job *job);

    /*
     * Global state (GS) API. These functions run under the BQL lock.
     *
     * See include/block/block-global-state.h for more information about
     * the GS API.
     */

    /**
     * Called when the job is resumed by the user (i.e. user_paused becomes
     * false). .user_resume is called before .resume.
     */
    void (*user_resume)(Job *job);

    /**
     * Optional callback for job types whose completion must be triggered
     * manually.
     */
    void (*complete)(Job *job, Error **errp);

    /**
     * If the callback is not NULL, prepare will be invoked when all the jobs
     * belonging to the same transaction complete; or upon this job's completion
     * if it is not in a transaction.
     *
     * This callback will not be invoked if the job has already failed.
     * If it fails, abort and then clean will be called.
     */
    int (*prepare)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked when all the jobs
     * belonging to the same transaction complete; or upon this job's
     * completion if it is not in a transaction. Skipped if NULL.
     *
     * All jobs will complete with a call to either .commit() or .abort() but
     * never both.
     */
    void (*commit)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked when any job in the
     * same transaction fails; or upon this job's failure (due to error or
     * cancellation) if it is not in a transaction. Skipped if NULL.
     *
     * All jobs will complete with a call to either .commit() or .abort() but
     * never both.
     */
    void (*abort)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked after a call to either
     * .commit() or .abort(). Regardless of which callback is invoked after
     * completion, .clean() will always be called, even if the job does not
     * belong to a transaction group.
     */
    void (*clean)(Job *job);

    /**
     * If the callback is not NULL, it will be invoked in job_cancel_async
     *
     * This function must return true if the job will be cancelled
     * immediately without any further I/O (mandatory if @force is
     * true), and false otherwise.  This lets the generic job layer
     * know whether a job has been truly (force-)cancelled, or whether
     * it is just in a special completion mode (like mirror after
     * READY).
     * (If the callback is NULL, the job is assumed to terminate
     * without I/O.)
     */
    bool (*cancel)(Job *job, bool force);


    /** Called when the job is freed */
    void (*free)(Job *job);
};

typedef enum JobCreateFlags {
    /* Default behavior */
    JOB_DEFAULT = 0x00,
    /* Job is not QMP-created and should not send QMP events */
    JOB_INTERNAL = 0x01,
    /* Job requires manual finalize step */
    JOB_MANUAL_FINALIZE = 0x02,
    /* Job requires manual dismiss step */
    JOB_MANUAL_DISMISS = 0x04,
} JobCreateFlags;

/**
 * job_lock:
 *
 * Take the mutex protecting the list of jobs and their status.
 * Most functions called by the monitor need to call job_lock
 * and job_unlock manually.  On the other hand, function called
 * by the block jobs themselves and by the block layer will take the
 * lock for you.
 */
void job_lock(void);

/**
 * job_unlock:
 *
 * Release the mutex protecting the list of jobs and their status.
 */
void job_unlock(void);

/** Returns the JobType of a given Job. */
JobType job_type(const Job *job);

/** Returns the enum string for the JobType of a given Job. */
const char *job_type_str(const Job *job);

#endif
