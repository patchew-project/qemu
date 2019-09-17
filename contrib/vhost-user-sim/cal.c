/*
 * vhost-user-sim calendar
 *
 * Copyright (c) 2019 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Johannes Berg <johannes.berg@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */
#include <stdbool.h>
#include <stdio.h>
#include "cal.h"

#define CAL_DEBUG 1

static unsigned long long simtime;
static G_LOCK_DEFINE(calendar);
static GSequence *calendar;
static GAsyncQueue *queue;
static bool scheduling;
static unsigned int required_clients;
static unsigned int running_clients;
static SimCalendarEntry *running_entry;

#define DPRINT(...) do {              \
    if (CAL_DEBUG) {                  \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);               \
    }                                 \
} while (0)

typedef enum {
    CAL_OP_INVALID,
    CAL_OP_RUN_DONE,
    CAL_OP_QUIT,
} CalMessage;

static void dump_scheduler(gpointer data, gpointer user)
{
    SimCalendarEntry *entry = data;

    DPRINT("<CAL>| %.20lld | %s\n", entry->time, entry->name);
}

static void dump_calendar(void)
{
    DPRINT("<CAL>|----------- calendar state at %lld\n", simtime);
    g_sequence_foreach(calendar, dump_scheduler, NULL);
}

unsigned long long calendar_get_time(void)
{
    return simtime;
}

static void count_clients(gpointer data, gpointer user)
{
    SimCalendarEntry *entry = data;
    unsigned int *count = user;

    if (entry->client) {
        (*count)++;
    }
}

static unsigned long long calendar_get_next_time(void)
{
    GSequenceIter *first;

    first = g_sequence_get_iter_at_pos(calendar, 0);
    if (!g_sequence_iter_is_end(first)) {
        SimCalendarEntry *next = g_sequence_get(first);
        return next->time;
    }

    return (unsigned long long)-1;
}

void calendar_set_time(unsigned long long time)
{
    g_assert(time >= simtime);
    G_LOCK(calendar);
    if (time > calendar_get_next_time()) {
        dump_calendar();
        DPRINT("CAL: setting time to %lld which is > %lld\n",
                time, calendar_get_next_time());
        g_assert(0);
    }
    simtime = time;
    G_UNLOCK(calendar);
}

static void calendar_schedule(void)
{
    GSequenceIter *first;
    SimCalendarEntry *entry;
    unsigned long long nexttime;

    G_LOCK(calendar);
    if (CAL_DEBUG) {
        dump_calendar();
    }

    if (!scheduling) {
        unsigned int count = 0;

        g_sequence_foreach(calendar, count_clients, &count);

        if (count < required_clients) {
            G_UNLOCK(calendar);
            return;
        }
        scheduling = true;
    }

    first = g_sequence_get_iter_at_pos(calendar, 0);
    if (g_sequence_iter_is_end(first)) {
        /* Everything disappeared, stop the simulation. */
        g_async_queue_push(queue, (void *)CAL_OP_QUIT);
        G_UNLOCK(calendar);
        return;
    }

    entry = g_sequence_get(first);
    g_sequence_remove(first);
    entry->iter = NULL;
    simtime = entry->time;
    entry->running = true;
    running_clients++;

    nexttime = calendar_get_next_time();
    running_entry = entry;
    G_UNLOCK(calendar);

    if (entry->update_until) {
        DPRINT("update %s to be free until %lld\n", entry->name, nexttime);
        entry->update_until(entry, nexttime);
    }
    entry->callback(entry);
}

static int entry_cmp_func(gconstpointer _a, gconstpointer _b, gpointer data)
{
    const SimCalendarEntry *a = _a;
    const SimCalendarEntry *b = _b;

    if (a->time == b->time) {
        return 0;
    }

    if (a->time < b->time) {
        return -1;
    }

    return 1;
}

void calendar_entry_add_unless_present(SimCalendarEntry *entry,
                                       unsigned long long time)
{
    G_LOCK(calendar);
    if (!entry->iter) {
        entry->time = time;
        entry->iter = g_sequence_insert_sorted(calendar, entry,
                                               entry_cmp_func, NULL);
        g_assert(entry->iter);
        dump_calendar();
        if (running_entry && entry != running_entry && running_entry->update_until) {
            unsigned long long nexttime = calendar_get_next_time();

            DPRINT("update %s to be free until %lld (due to add unless of %s)\n", running_entry->name, nexttime, entry->name);
            running_entry->update_until(running_entry, nexttime);
        } else {
            DPRINT("no update for running entry %s\n", running_entry ? running_entry->name : "<none>");
        }
    }
    G_UNLOCK(calendar);
}

void calendar_entry_add(SimCalendarEntry *entry)
{
    G_LOCK(calendar);
    g_assert(!entry->iter);
    entry->iter = g_sequence_insert_sorted(calendar, entry,
                                           entry_cmp_func, NULL);
    g_assert(entry->iter);
    dump_calendar();
    if (running_entry && entry != running_entry &&
        running_entry->update_until) {
        unsigned long long nexttime = calendar_get_next_time();

        DPRINT("update %s to be free until %lld (due to add of %s)\n", running_entry->name, nexttime, entry->name);
        running_entry->update_until(running_entry, nexttime);
    } else {
        DPRINT("no update for running entry %s\n", running_entry ? running_entry->name : "<none>");
    }
    G_UNLOCK(calendar);
}

static void _calendar_run_done(SimCalendarEntry *entry)
{
    /*
     * This will happen while new clients join the
     * simulation and go into wait - their events
     * will be marked as "done" even though they
     * never started ...
     * Currently we don't let any clients join the
     * simulation after it has started, but that's
     * not really controllable anyway, so better.
     */
    if (scheduling) {
        g_assert(entry->running);
        entry->running = false;
        running_clients--;
        g_assert(running_clients == 0);
    }
    g_async_queue_push(queue, (void *)CAL_OP_RUN_DONE);
}

static bool _calendar_entry_remove(SimCalendarEntry *entry)
{
    if (entry->iter) {
        g_sequence_remove(entry->iter);
        entry->iter = NULL;
        return true;
    }

    return false;
}

bool calendar_entry_remove(SimCalendarEntry *entry)
{
    bool scheduled;

    G_LOCK(calendar);
    scheduled = _calendar_entry_remove(entry);
    G_UNLOCK(calendar);

    return scheduled;
}

void calendar_run_done(SimCalendarEntry *entry)
{
    G_LOCK(calendar);
    _calendar_run_done(entry);
    G_UNLOCK(calendar);
}

void calendar_entry_destroy(SimCalendarEntry *entry)
{
    G_LOCK(calendar);
    if (running_entry == entry) {
        running_entry = NULL;
    }
    if (entry->running) {
        DPRINT("destroying running client %s\n", entry->name);
        _calendar_run_done(entry);
    }
    _calendar_entry_remove(entry);
    G_UNLOCK(calendar);
}

void calendar_init(unsigned int required)
{
    calendar = g_sequence_new(NULL);
    queue = g_async_queue_new();
    required_clients = required;
}

void calendar_run(void)
{
    while (1) {
        CalMessage msg = (CalMessage)g_async_queue_pop(queue);

        switch (msg) {
        case CAL_OP_INVALID:
            g_assert(0);
            break;
        case CAL_OP_RUN_DONE:
            calendar_schedule();
            break;
        case CAL_OP_QUIT:
            return;
        }
    }
}
