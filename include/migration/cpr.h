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

#define CPR_MODE_ALL CPR_MODE__MAX

int cpr_add_blocker(Error **reasonp, Error **errp, CprMode mode, ...);
int cpr_add_blocker_str(const char *reason, Error **errp, CprMode mode, ...);
void cpr_del_blocker(Error **reasonp);

#endif
