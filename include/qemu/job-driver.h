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

#ifndef JOB_DRIVER_H
#define JOB_DRIVER_H

#include "job-common.h"

/*
 * Job driver API.
 *
 * These functions use are used by job drivers like mirror,
 * stream, commit etc. The driver is not aware of the job_mutex
 * presence, so these functions use it internally to protect
 * job fields (see job-common.h).
 *
 * Therefore, each function in this API that requires protection
 * must have the comment
 * "Called with job_mutext *not* held"
 * in job.c
 */

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

/**
 * Returns whether the job is scheduled for cancellation (at an
 * indefinite point).
 */
bool job_cancel_requested(Job *job);

/** Returns whether the job is scheduled for cancellation. */
bool job_is_cancelled(Job *job);

/** Returns whether the job is ready to be completed. */
bool job_is_ready(Job *job);

/** The @job could not be started, free it. */
void job_early_fail(Job *job);

/** Moves the @job from RUNNING to READY */
void job_transition_to_ready(Job *job);

/** Enters the @job if it is not paused */
void job_enter_not_paused(Job *job);

/** returns @job->ret */
bool job_has_failed(Job *job);

/** Returns the @job->status */
JobStatus job_get_status(Job *job);

/** Returns the @job->pause_count */
int job_get_pause_count(Job *job);

/** Returns @job->paused */
bool job_get_paused(Job *job);

/** Returns @job->busy */
bool job_get_busy(Job *job);

/** Return true if @job not paused and not cancelled */
bool job_not_paused_nor_cancelled(Job *job);

#endif /* JOB_DRIVER_H */
