/*
 * QEMU System Emulator block accounting
 *
 * Copyright (c) 2011 Christoph Hellwig
 * Copyright (c) 2015 Igalia, S.L.
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
#include "block/accounting.h"
#include "block/block_int.h"
#include "qemu/timer.h"
#include "sysemu/qtest.h"

static QEMUClockType clock_type = QEMU_CLOCK_REALTIME;
static const int qtest_latency_ns = NANOSECONDS_PER_SECOND / 1000;

void block_acct_init(BlockAcctStats *stats)
{
    qemu_mutex_init(&stats->lock);
    if (qtest_enabled()) {
        clock_type = QEMU_CLOCK_VIRTUAL;
    }
}

void block_acct_setup(BlockAcctStats *stats, bool account_invalid,
                      bool account_failed)
{
    stats->account_invalid = account_invalid;
    stats->account_failed = account_failed;
}

void block_acct_cleanup(BlockAcctStats *stats)
{
    BlockAcctTimedStats *s, *next;
    QSLIST_FOREACH_SAFE(s, &stats->intervals, entries, next) {
        g_free(s);
    }
    qemu_mutex_destroy(&stats->lock);
}

void block_acct_add_interval(BlockAcctStats *stats, unsigned interval_length)
{
    BlockAcctTimedStats *s;
    unsigned i;

    s = g_new0(BlockAcctTimedStats, 1);
    s->interval_length = interval_length;
    s->stats = stats;
    qemu_mutex_lock(&stats->lock);
    QSLIST_INSERT_HEAD(&stats->intervals, s, entries);

    for (i = 0; i < BLOCK_MAX_IOTYPE; i++) {
        timed_average_init(&s->latency[i], clock_type,
                           (uint64_t) interval_length * NANOSECONDS_PER_SECOND);
    }
    qemu_mutex_unlock(&stats->lock);
}

BlockAcctTimedStats *block_acct_interval_next(BlockAcctStats *stats,
                                              BlockAcctTimedStats *s)
{
    if (s == NULL) {
        return QSLIST_FIRST(&stats->intervals);
    } else {
        return QSLIST_NEXT(s, entries);
    }
}

void block_acct_start(BlockAcctStats *stats, BlockAcctCookie *cookie,
                      int64_t bytes, enum BlockAcctType type)
{
    assert(type < BLOCK_MAX_IOTYPE);

    cookie->bytes = bytes;
    cookie->start_time_ns = qemu_clock_get_ns(clock_type);
    cookie->type = type;
}

/* block_latency_histogram_compare_func
 * Compare @key with interval [@el, @el+1), where @el+1 is a next array element
 * after @el.
 * Return: -1 if @key < @el
 *          0 if @key in [@el, @el+1)
 *         +1 if @key >= @el+1
 */
static int block_latency_histogram_compare_func(const void *key, const void *el)
{
    uint64_t k = *(uint64_t *)key;
    uint64_t a = *(uint64_t *)el;
    uint64_t b = *((uint64_t *)el + 1);

    return k < a ? -1 : (k < b ? 0 : 1);
}

static void block_latency_histogram_account(BlockLatencyHistogram *hist,
                                            enum BlockAcctType type,
                                            int64_t latency_ns)
{
    uint64_t *data, *pos;

    if (hist->points == NULL) {
        /* histogram disabled */
        return;
    }

    data = hist->histogram[type];

    if (latency_ns < hist->points[0]) {
        data[0]++;
        return;
    }

    if (latency_ns >= hist->points[hist->size - 2]) {
        data[hist->size - 1]++;
        return;
    }

    pos = bsearch(&latency_ns, hist->points, hist->size - 2,
                  sizeof(hist->points[0]),
                  block_latency_histogram_compare_func);
    assert(pos != NULL);

    data[pos - hist->points + 1]++;
}

int block_latency_histogram_set(BlockAcctStats *stats, uint64List *latency)
{
    BlockLatencyHistogram *hist = &stats->latency_histogram;
    uint64List *entry;
    uint64_t *ptr;
    int i;
    uint64_t prev = 0;

    hist->size = 1;

    for (entry = latency; entry; entry = entry->next) {
        if (entry->value <= prev) {
            return -EINVAL;
        }
        hist->size++;
        prev = entry->value;
    }

    hist->points = g_renew(uint64_t, hist->points, hist->size - 1);
    for (entry = latency, ptr = hist->points; entry;
         entry = entry->next, ptr++)
    {
        *ptr = entry->value;
    }

    for (i = 0; i < BLOCK_MAX_IOTYPE; i++) {
        hist->histogram[i] = g_renew(uint64_t, hist->histogram[i], hist->size);
        memset(hist->histogram[i], 0, hist->size * sizeof(uint64_t));
    }

    return 0;
}

void block_latency_histogram_clear(BlockAcctStats *stats)
{
    BlockLatencyHistogram *hist = &stats->latency_histogram;
    int i;

    g_free(hist->points);
    hist->points = NULL;

    for (i = 0; i < BLOCK_MAX_IOTYPE; i++) {
        g_free(hist->histogram[i]);
        hist->histogram[i] = NULL;
    }
}

static void block_account_one_io(BlockAcctStats *stats, BlockAcctCookie *cookie,
                                 bool failed)
{
    BlockAcctTimedStats *s;
    int64_t time_ns = qemu_clock_get_ns(clock_type);
    int64_t latency_ns = time_ns - cookie->start_time_ns;

    if (qtest_enabled()) {
        latency_ns = qtest_latency_ns;
    }

    assert(cookie->type < BLOCK_MAX_IOTYPE);

    qemu_mutex_lock(&stats->lock);

    if (failed) {
        stats->failed_ops[cookie->type]++;
    } else {
        stats->nr_bytes[cookie->type] += cookie->bytes;
        stats->nr_ops[cookie->type]++;
    }

    block_latency_histogram_account(&stats->latency_histogram, cookie->type,
                                    latency_ns);

    if (!failed || stats->account_failed) {
        stats->total_time_ns[cookie->type] += latency_ns;
        stats->last_access_time_ns = time_ns;

        QSLIST_FOREACH(s, &stats->intervals, entries) {
            timed_average_account(&s->latency[cookie->type], latency_ns);
        }
    }

    qemu_mutex_unlock(&stats->lock);
}

void block_acct_done(BlockAcctStats *stats, BlockAcctCookie *cookie)
{
    block_account_one_io(stats, cookie, false);
}

void block_acct_failed(BlockAcctStats *stats, BlockAcctCookie *cookie)
{
    block_account_one_io(stats, cookie, true);
}

void block_acct_invalid(BlockAcctStats *stats, enum BlockAcctType type)
{
    assert(type < BLOCK_MAX_IOTYPE);

    /* block_account_one_io() updates total_time_ns[], but this one does
     * not.  The reason is that invalid requests are accounted during their
     * submission, therefore there's no actual I/O involved.
     */
    qemu_mutex_lock(&stats->lock);
    stats->invalid_ops[type]++;

    if (stats->account_invalid) {
        stats->last_access_time_ns = qemu_clock_get_ns(clock_type);
    }
    qemu_mutex_unlock(&stats->lock);
}

void block_acct_merge_done(BlockAcctStats *stats, enum BlockAcctType type,
                      int num_requests)
{
    assert(type < BLOCK_MAX_IOTYPE);

    qemu_mutex_lock(&stats->lock);
    stats->merged[type] += num_requests;
    qemu_mutex_unlock(&stats->lock);
}

int64_t block_acct_idle_time_ns(BlockAcctStats *stats)
{
    return qemu_clock_get_ns(clock_type) - stats->last_access_time_ns;
}

double block_acct_queue_depth(BlockAcctTimedStats *stats,
                              enum BlockAcctType type)
{
    uint64_t sum, elapsed;

    assert(type < BLOCK_MAX_IOTYPE);

    qemu_mutex_lock(&stats->stats->lock);
    sum = timed_average_sum(&stats->latency[type], &elapsed);
    qemu_mutex_unlock(&stats->stats->lock);

    return (double) sum / elapsed;
}
