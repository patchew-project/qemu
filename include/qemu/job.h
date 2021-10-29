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

#ifndef JOB_H
#define JOB_H

#include "job-common.h"

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
 */
void job_txn_add_job(JobTxn *txn, Job *job);

/**
 * Create a new long-running job and return it.
 *
 * @job_id: The id of the newly-created job, or %NULL for internal jobs
 * @driver: The class object for the newly-created job.
 * @txn: The transaction this job belongs to, if any. %NULL otherwise.
 * @ctx: The AioContext to run the job coroutine in.
 * @flags: Creation flags for the job. See @JobCreateFlags.
 * @cb: Completion function for the job.
 * @opaque: Opaque pointer value passed to @cb.
 * @errp: Error object.
 */
void *job_create(const char *job_id, const JobDriver *driver, JobTxn *txn,
                 AioContext *ctx, int flags, BlockCompletionFunc *cb,
                 void *opaque, Error **errp);

/**
 * Add a reference to Job refcnt, it will be decreased with job_unref, and then
 * be freed if it comes to be the last reference.
 */
void job_ref(Job *job);

/**
 * Release a reference that was previously acquired with job_ref() or
 * job_create(). If it's the last reference to the object, it will be freed.
 */
void job_unref(Job *job);

/**
 * @job: The job that has made progress
 * @done: How much progress the job made since the last call
 *
 * Updates the progress counter of the job.
 */
void job_progress_update(Job *job, uint64_t done);

/**
 * @job: The job whose expected progress end value is set
 * @remaining: Missing progress (on top of the current progress counter value)
 *             until the new expected end value is reached
 *
 * Sets the expected end value of the progress counter of a job so that a
 * completion percentage can be calculated when the progress is updated.
 */
void job_progress_set_remaining(Job *job, uint64_t remaining);

/**
 * @job: The job whose expected progress end value is updated
 * @delta: Value which is to be added to the current expected end
 *         value
 *
 * Increases the expected end value of the progress counter of a job.
 * This is useful for parenthesis operations: If a job has to
 * conditionally perform a high-priority operation as part of its
 * progress, it calls this function with the expected operation's
 * length before, and job_progress_update() afterwards.
 * (So the operation acts as a parenthesis in regards to the main job
 * operation running in background.)
 */
void job_progress_increase_remaining(Job *job, uint64_t delta);

/** To be called when a cancelled job is finalised. */
void job_event_cancelled(Job *job);

/** To be called when a successfully completed job is finalised. */
void job_event_completed(Job *job);

/**
 * Conditionally enter the job coroutine if the job is ready to run, not
 * already busy and fn() returns true. fn() is called while under the job_lock
 * critical section.
 */
void job_enter_cond(Job *job, bool(*fn)(Job *job));

/**
 * @job: A job that has not yet been started.
 *
 * Begins execution of a job.
 * Takes ownership of one reference to the job object.
 */
void job_start(Job *job);

/**
 * @job: The job to enter.
 *
 * Continue the specified job by entering the coroutine.
 */
void job_enter(Job *job);

/**
 * @job: The job that is ready to pause.
 *
 * Pause now if job_pause() has been called. Jobs that perform lots of I/O
 * must call this between requests so that the job can be paused.
 */
void coroutine_fn job_pause_point(Job *job);

/**
 * @job: The job that calls the function.
 *
 * Yield the job coroutine.
 */
void job_yield(Job *job);

/**
 * @job: The job that calls the function.
 * @ns: How many nanoseconds to stop for.
 *
 * Put the job to sleep (assuming that it wasn't canceled) for @ns
 * %QEMU_CLOCK_REALTIME nanoseconds.  Canceling the job will immediately
 * interrupt the wait.
 */
void coroutine_fn job_sleep_ns(Job *job, int64_t ns);


/** Returns true if the job should not be visible to the management layer. */
bool job_is_internal(Job *job);

/** Returns whether the job is being cancelled. */
bool job_is_cancelled(Job *job);

/**
 * Returns whether the job is scheduled for cancellation (at an
 * indefinite point).
 */
bool job_cancel_requested(Job *job);

/** Returns whether the job is in a completed state. */
bool job_is_completed(Job *job);

/** Returns whether the job is ready to be completed. */
bool job_is_ready(Job *job);

/**
 * Request @job to pause at the next pause point. Must be paired with
 * job_resume(). If the job is supposed to be resumed by user action, call
 * job_user_pause() instead.
 */
void job_pause(Job *job);

/** Resumes a @job paused with job_pause. */
void job_resume(Job *job);

/**
 * Asynchronously pause the specified @job.
 * Do not allow a resume until a matching call to job_user_resume.
 */
void job_user_pause(Job *job, Error **errp);

/** Returns true if the job is user-paused. */
bool job_user_paused(Job *job);

/**
 * Resume the specified @job.
 * Must be paired with a preceding job_user_pause.
 */
void job_user_resume(Job *job, Error **errp);

/**
 * Get the next element from the list of block jobs after @job, or the
 * first one if @job is %NULL.
 *
 * Returns the requested job, or %NULL if there are no more jobs left.
 */
Job *job_next(Job *job);

/**
 * Get the job identified by @id (which must not be %NULL).
 *
 * Returns the requested job, or %NULL if it doesn't exist.
 */
Job *job_get(const char *id);

/**
 * Check whether the verb @verb can be applied to @job in its current state.
 * Returns 0 if the verb can be applied; otherwise errp is set and -EPERM
 * returned.
 */
int job_apply_verb(Job *job, JobVerb verb, Error **errp);

/** The @job could not be started, free it. */
void job_early_fail(Job *job);

/** Moves the @job from RUNNING to READY */
void job_transition_to_ready(Job *job);

/** Asynchronously complete the specified @job. */
void job_complete(Job *job, Error **errp);

/**
 * Asynchronously cancel the specified @job. If @force is true, the job should
 * be cancelled immediately without waiting for a consistent state.
 */
void job_cancel(Job *job, bool force);

/**
 * Cancels the specified job like job_cancel(), but may refuse to do so if the
 * operation isn't meaningful in the current state of the job.
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

/** Synchronously force-cancels all jobs using job_cancel_sync(). */
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
 */
int job_complete_sync(Job *job, Error **errp);

/**
 * For a @job that has finished its work and is pending awaiting explicit
 * acknowledgement to commit its work, this will commit that work.
 *
 * FIXME: Make the below statement universally true:
 * For jobs that support the manual workflow mode, all graph changes that occur
 * as a result will occur after this command and before a successful reply.
 */
void job_finalize(Job *job, Error **errp);

/**
 * Remove the concluded @job from the query list and resets the passed pointer
 * to %NULL. Returns an error if the job is not actually concluded.
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
 */
int job_finish_sync(Job *job, void (*finish)(Job *, Error **errp), Error **errp);

#endif
