/*
 * Copyright (c) 2021, 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

#include "qapi/qapi-types-cpr.h"

void cpr_init(int modes);
void cpr_set_mode(CprMode mode);
CprMode cpr_get_mode(void);
bool cpr_enabled(CprMode mode);

typedef int (*cpr_walk_fd_cb)(const char *name, int id, int fd, void *opaque);

void cpr_save_fd(const char *name, int id, int fd);
void cpr_delete_fd(const char *name, int id);
int cpr_find_fd(const char *name, int id);
int cpr_walk_fd(cpr_walk_fd_cb cb, void *handle);
void cpr_save_memfd(const char *name, int fd, size_t len, size_t maxlen,
                    uint64_t align);
int cpr_find_memfd(const char *name, size_t *lenp, size_t *maxlenp,
                   uint64_t *alignp);
void cpr_delete_memfd(const char *name);
int cpr_resave_fd(const char *name, int id, int fd, Error **errp);
int cpr_state_save(Error **errp);
int cpr_state_load(Error **errp);
void cpr_state_print(void);

#define CPR_MODE_ALL CPR_MODE__MAX

int cpr_add_blocker(Error **reasonp, Error **errp, CprMode mode, ...);
int cpr_add_blocker_str(const char *reason, Error **errp, CprMode mode, ...);
void cpr_del_blocker(Error **reasonp);

#endif
