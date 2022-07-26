/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/cpr-state.h"

void cpr_save_fd(const char *name, int id, int fd)
{
}

void cpr_delete_fd(const char *name, int id)
{
}

int cpr_find_fd(const char *name, int id)
{
    return -1;
}

void cpr_resave_fd(const char *name, int id, int fd)
{
}
