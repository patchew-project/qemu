/*
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

#include "qapi/qapi-types-cpr.h"

void cprsave(const char *file, CprMode mode, Error **errp);
void cprexec(strList *args, Error **errp);
void cprload(const char *file, Error **errp);

#endif
