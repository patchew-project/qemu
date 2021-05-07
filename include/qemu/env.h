/*
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_ENV_H
#define QEMU_ENV_H

#define FD_PREFIX "QEMU_FD_"

typedef int (*walkenv_cb)(const char *name, const char *val, void *handle);

int getenv_fd(const char *name);
void setenv_fd(const char *name, int fd);
void unsetenv_fd(const char *name);
void unsetenv_fdv(const char *fmt, ...);
int walkenv(const char *prefix, walkenv_cb cb, void *handle);
void printenv(void);

#endif
