/*
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef RUNSTATE_ACTION_H
#define RUNSTATE_ACTION_H

/* in softmmu/runstate-action.c */
int process_runstate_actions(void *opaque, QemuOpts *opts, Error **errp);
int runstate_action_parse(QemuOptsList *opts_list, const char *optarg);

#endif /* RUNSTATE_ACTION_H */
