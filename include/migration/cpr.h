/*
 * Copyright (c) 2021, 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

bool cpr_needed_for_exec(void *opaque);
void cpr_unpreserve_fds(void);

#endif
