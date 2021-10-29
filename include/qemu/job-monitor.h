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

#ifndef JOB_MONITOR_H
#define JOB_MONITOR_H

#include "job-common.h"

/*
 * Job monitor API.
 *
 * These functions use are used by the QEMU monitor, for example
 * to execute QMP commands. The monitor is aware of the job_mutex
 * presence, so these functions assume it is held by the caller
 * to protect job fields (see job-common.h).
 * This prevents TOC/TOU bugs, allowing the caller to hold the
 * lock between a check in the job state and the actual action.
 *
 * Therefore, each function in this API that needs protection
 * must have the comment
 * "Called between job_lock and job_unlock."
 */

/**
 * Allocate and return a new job transaction. Jobs can be added to the
 * transaction using job_txn_add_job().
 *
 * The transaction is automatically freed when the last job completes or is
 * cancelled.
 *
 * All jobs in the transaction either complete successfully or fail/cancel as a
 * group.  Jobs wait for each other before completing.  Cancelling one job
 * cancels all jobs in the transaction.
 */
JobTxn *job_txn_new(void);

/**
 * Release a reference that was previously acquired with job_txn_add_job or
 * job_txn_new. If it's the last reference to the object, it will be freed.
 */
void job_txn_unref(JobTxn *txn);

/**
 * @txn: The transaction (may be NULL)
 * @job: Job to add to the transaction
 *
 * Add @job to the transaction.  The @job must not already be in a transaction.
 * The caller must call either job_txn_unref() or job_completed() to release
 * the reference that is automatically grabbed here.
 *
 * If @txn is NULL, the function does nothing.
 *
 * Called between job_lock and job_unlock.
 */
void job_txn_add_job(JobTxn *txn, Job *job);

/**
 * Add a reference to Job refcnt, it will be decreased with job_unref, and then
 * be freed if it comes to be the last reference.
 *
 * Called between job_lock and job_unlock.
 */
void job_ref(Job *job);

/**
 * Release a reference that was previously acquired with job_ref() or
 * job_create(). If it's the last reference to the object, it will be freed.
 *
 * Called between job_lock and job_unlock.
 */
void job_unref(Job *job);

/**
 * Conditionally enter the job coroutine if the job is ready to run, not
 * already busy and fn() returns true. fn() is called while under the job_lock
 * critical section.
 *
 * Called between job_lock and job_unlock, but it releases the lock temporarly.
 */
void job_enter_cond(Job *job, bool(*fn)(Job *job));

/**
 * Returns true if the job should not be visible to the management layer.
 */
bool job_is_internal(Job *job);

/**
 * Returns whether the job is in a completed state.
 * Called between job_lock and job_unlock.
 */
bool job_is_completed(Job *job);

/**
 * Request @job to pause at the next pause point. Must be paired with
 * job_resume(). If the job is supposed to be resumed by user action, call
 * job_user_pause() instead.
 *
 * Called between job_lock and job_unlock.
 */
void job_pause(Job *job);

/**
 * Resumes a @job paused with job_pause.
 * Called between job_lock and job_unlock.
 */
void job_resume(Job *job);

/**
 * Asynchronously pause the specified @job.
 * Do not allow a resume until a matching call to job_user_resume.
 *
 * Called between job_lock and job_unlock.
 */
void job_user_pause(Job *job, Error **errp);

/**
 * Returns true if the job is user-paused.
 * Called between job_lock and job_unlock.
 */
bool job_user_paused(Job *job);

/**
 * Resume the specified @job.
 * Must be paired with a preceding job_user_pause.
 *
 * Called between job_lock and job_unlock.
 */
void job_user_resume(Job *job, Error **errp);

/**
 * Get the next element from the list of block jobs after @job, or the
 * first one if @job is %NULL.
 *
 * Returns the requested job, or %NULL if there are no more jobs left.
 *
 * Called between job_lock and job_unlock.
 */
Job *job_next(Job *job);

/**
 * Get the job identified by @id (which must not be %NULL).
 *
 * Returns the requested job, or %NULL if it doesn't exist.
 *
 * Called between job_lock and job_unlock.
 */
Job *job_get(const char *id);

/**
 * Check whether the verb @verb can be applied to @job in its current state.
 * Returns 0 if the verb can be applied; otherwise errp is set and -EPERM
 * returned.
 *
 * Called between job_lock and job_unlock.
 */
int job_apply_verb(Job *job, JobVerb verb, Error **errp);

/**
 * Asynchronously complete the specified @job.
 * Called between job_lock and job_unlock, but it releases the lock temporarly.
 */
void job_complete(Job *job, Error **errp);

/**
 * Asynchronously cancel the specified @job. If @force is true, the job should
 * be cancelled immediately without waiting for a consistent state.
 *
 * Called between job_lock and job_unlock.
 */
void job_cancel(Job *job, bool force);

/**
 * Cancels the specified job like job_cancel(), but may refuse to do so if the
 * operation isn't meaningful in the current state of the job.
 *
 * Called between job_lock and job_unlock.
 */
void job_user_cancel(Job *job, bool force, Error **errp);

/**
 * Synchronously cancel the @job.  The completion callback is called
 * before the function returns.  If @force is false, the job may
 * actually complete instead of canceling itself; the circumstances
 * under which this happens depend on the kind of job that is active.
 *
 * Returns the return value from the job if the job actually completed
 * during the call, or -ECANCELED if it was canceled.
 *
 * Callers must hold the AioContext lock of job->aio_context.
 */
int job_cancel_sync(Job *job, bool force);

/**
 * Synchronously force-cancels all jobs using job_cancel_sync().
 *
 * Called with job_lock *not* held, unlike most other APIs consumed
 * by the monitor! This is primarly to avoid adding unnecessary lock-unlock
 * patterns in the caller.
 */
void job_cancel_sync_all(void);

/**
 * @job: The job to be completed.
 * @errp: Error object which may be set by job_complete(); this is not
 *        necessarily set on every error, the job return value has to be
 *        checked as well.
 *
 * Synchronously complete the job.  The completion callback is called before the
 * function returns, unless it is NULL (which is permissible when using this
 * function).
 *
 * Returns the return value from the job.
 *
 * Callers must hold the AioContext lock of job->aio_context.
 *
 * Called between job_lock and job_unlock.
 */
int job_complete_sync(Job *job, Error **errp);

/**
 * For a @job that has finished its work and is pending awaiting explicit
 * acknowledgement to commit its work, this will commit that work.
 *
 * FIXME: Make the below statement universally true:
 * For jobs that support the manual workflow mode, all graph changes that occur
 * as a result will occur after this command and before a successful reply.
 *
 * Called between job_lock and job_unlock.
 */
void job_finalize(Job *job, Error **errp);

/**
 * Remove the concluded @job from the query list and resets the passed pointer
 * to %NULL. Returns an error if the job is not actually concluded.
 *
 * Called between job_lock and job_unlock.
 */
void job_dismiss(Job **job, Error **errp);

/**
 * Synchronously finishes the given @job. If @finish is given, it is called to
 * trigger completion or cancellation of the job.
 *
 * Returns 0 if the job is successfully completed, -ECANCELED if the job was
 * cancelled before completing, and -errno in other error cases.
 *
 * Callers must hold the AioContext lock of job->aio_context.
 *
 * Called between job_lock and job_unlock, but it releases the lock temporarly.
 */
int job_finish_sync(Job *job, void (*finish)(Job *, Error **errp), Error **errp);

/** Same as job_is_ready(), but assumes job_lock is held. */
bool job_is_ready_locked(Job *job);

/** Same as job_early_fail(), but assumes job_lock is held. */
void job_early_fail_locked(Job *job);

#endif
