/*
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/env.h"

static uint64_t getenv_ulong(const char *prefix, const char *name, int *err)
{
    char var[80], *val;
    uint64_t res = 0;

    snprintf(var, sizeof(var), "%s%s", prefix, name);
    val = getenv(var);
    if (val) {
        *err = qemu_strtoul(val, NULL, 10, &res);
    } else {
        *err = -ENOENT;
    }
    return res;
}

static void setenv_ulong(const char *prefix, const char *name, uint64_t val)
{
    char var[80], val_str[80];
    snprintf(var, sizeof(var), "%s%s", prefix, name);
    snprintf(val_str, sizeof(val_str), "%"PRIu64, val);
    setenv(var, val_str, 1);
}

static void unsetenv_ulong(const char *prefix, const char *name)
{
    char var[80];
    snprintf(var, sizeof(var), "%s%s", prefix, name);
    unsetenv(var);
}

int getenv_fd(const char *name)
{
    int err;
    int fd = getenv_ulong(FD_PREFIX, name, &err);
    return err ? -1 : fd;
}

void setenv_fd(const char *name, int fd)
{
    setenv_ulong(FD_PREFIX, name, fd);
}

void unsetenv_fd(const char *name)
{
    unsetenv_ulong(FD_PREFIX, name);
}

void unsetenv_fdv(const char *fmt, ...)
{
    va_list args;
    char buf[80];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
}

int walkenv(const char *prefix, walkenv_cb cb, void *handle)
{
    char *str, name[128];
    char **envp = environ;
    size_t prefix_len = strlen(prefix);

    while (*envp) {
        str = *envp++;
        if (!strncmp(str, prefix, prefix_len)) {
            char *val = strchr(str, '=');
            str += prefix_len;
            strncpy(name, str, val - str);
            name[val - str] = 0;
            if (cb(name, val + 1, handle)) {
                return 1;
            }
        }
    }
    return 0;
}

void printenv(void)
{
    char **ptr = environ;
    while (*ptr) {
        puts(*ptr++);
    }
}
