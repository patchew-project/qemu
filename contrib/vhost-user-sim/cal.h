/*
 * vhost-user-sim calendar (header file)
 *
 * Copyright (c) 2019 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Johannes Berg <johannes.berg@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */
#ifndef _SIM_CAL_H
#define _SIM_CAL_H
#include <stdbool.h>
#include <gmodule.h>

typedef struct SimCalendarEntry SimCalendarEntry;
typedef void (*start_callback_t)(SimCalendarEntry *entry);
typedef void (*update_until_callback_t)(SimCalendarEntry *entry,
                                        unsigned long long until);

struct SimCalendarEntry {
    unsigned long long time;
    start_callback_t callback;
    update_until_callback_t update_until;
    gchar *name;
    GSequenceIter *iter;
    bool running;
    bool client;
};

void calendar_init(unsigned int required_clients);
void calendar_run(void);

unsigned long long calendar_get_time(void);
void calendar_set_time(unsigned long long time);
void calendar_entry_add(SimCalendarEntry *entry);
void calendar_entry_add_unless_present(SimCalendarEntry *entry,
                                       unsigned long long time);
bool calendar_entry_remove(SimCalendarEntry *entry);
void calendar_entry_destroy(SimCalendarEntry *entry);

void calendar_run_done(SimCalendarEntry *entry);

#endif /* _SIM_CAL_H */
