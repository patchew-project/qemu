/*
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

#include "qapi/qapi-types-cpr.h"

#define CPR_MODE_NONE ((CprMode)(-1))

CprMode cpr_mode(void);

typedef int (*cpr_walk_fd_cb)(const char *name, int id, int fd, void *opaque);

void cpr_save_fd(const char *name, int id, int fd);
void cpr_delete_fd(const char *name, int id);
int cpr_find_fd(const char *name, int id);
int cpr_walk_fd(cpr_walk_fd_cb cb, void *handle);
int cpr_state_save(Error **errp);
int cpr_state_load(Error **errp);
CprMode cpr_state_mode(void);
void cpr_state_print(void);

#endif
