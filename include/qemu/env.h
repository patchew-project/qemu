/*
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_ENV_H
#define QEMU_ENV_H

#define FD_PREFIX "QEMU_FD_"
#define ADDR_PREFIX "QEMU_ADDR_"
#define LEN_PREFIX "QEMU_LEN_"
#define BOOL_PREFIX "QEMU_BOOL_"

typedef int (*walkenv_cb)(const char *name, const char *val, void *handle);

bool getenv_ram(const char *name, void **addrp, size_t *lenp);
void setenv_ram(const char *name, void *addr, size_t len);
void unsetenv_ram(const char *name);
int getenv_fd(const char *name);
void setenv_fd(const char *name, int fd);
void unsetenv_fd(const char *name);
bool getenv_bool(const char *name);
void setenv_bool(const char *name, bool val);
void unsetenv_bool(const char *name);
int walkenv(const char *prefix, walkenv_cb cb, void *handle);
void printenv(void);

#endif
