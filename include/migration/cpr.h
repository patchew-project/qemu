/*
 * Copyright (c) 2021, 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

extern bool only_cpr_capable;

void cpr_init(void);
void cpr_exec(void);
void cpr_exec_failed(Error *err);
void cpr_preserve_fds(void);

#endif
