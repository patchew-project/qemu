/*
 * Guest-side management of hypertrace.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu-hypertrace.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <glob.h>

#include "config-host.h"
#include "config-target.h"
#if defined(CONFIG_SOFTMMU)
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#endif


static char *data_path = NULL;
static char *control_path = NULL;
static int data_fd = -1;
static int control_fd = -1;

static uint64_t *data_addr = NULL;
static uint64_t *control_addr = NULL;


static int init_channel_file(const char *base, const char *suffix, size_t size,
                             char ** path, int *fd, uint64_t **addr)
{
    *path = malloc(strlen(base) + strlen(suffix) + 1);
    sprintf(*path, "%s%s", base, suffix);

    *fd = open(*path, O_RDWR);
    if (*fd == -1) {
        return -1;
    }

    *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    if (*addr == MAP_FAILED) {
        return -1;
    }
    return 0;
}

#if !defined(CONFIG_USER_ONLY) && defined(__linux__)
static int check_device_id (const char *base, const char *name, uint64_t value)
{
    char tmp[1024];
    sprintf(tmp, "%s/%s", base, name);

    int fd = open(tmp, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    char v[1024];
    ssize_t s = read(fd, v, sizeof(v));
    if (s < 0) {
        close(fd);
        return -1;
    }
    v[s] = '\0';

    char *end;
    uint64_t vv = strtoull(v, &end, 16);
    if (*end == '\n' && vv == value) {
        return 0;
    }
    else {
        return -1;
    }
}

static char* find_device(void)
{
    static char tmp[1024];
    char *res = NULL;

    glob_t g;
    if (glob("/sys/devices/pci*/*", GLOB_NOSORT, NULL, &g) != 0) {
        return NULL;
    }


    int i;
    for (i = 0; i < g.gl_pathc; i++) {
        char *path = g.gl_pathv[i];

        if (check_device_id(path, "vendor", PCI_VENDOR_ID_REDHAT_QUMRANET) < 0) {
            continue;
        }
        if (check_device_id(path, "device", PCI_DEVICE_ID_HYPERTRACE) < 0) {
            continue;
        }

        sprintf(tmp, "%s", path);
        res = tmp;
        break;
    }

    globfree(&g);

    return res;
}
#endif

int qemu_hypertrace_init(const char *base)
{
#if defined(CONFIG_USER_ONLY)
    const char *control_suff = "-control";
    const size_t control_size = getpagesize() * 2;
    const char *data_suff = "-data";
#elif defined(__linux__)
    const char *control_suff = "/resource0";
    const size_t control_size = getpagesize();
    const char *data_suff = "/resource1";
#else
#error Unsupported OS
#endif

#if defined(CONFIG_USER_ONLY)
    if (base == NULL) {
        errno = ENOENT;
        return -1;
    }
#elif defined(__linux__)
    if (base == NULL) {
        /* try to guess the base path */
        base = find_device();
        if (base == NULL) {
            errno = ENOENT;
            return -1;
        }
    }
#endif

    int res;
    res = init_channel_file(base, control_suff, control_size,
                            &control_path, &control_fd, &control_addr);
    if (res != 0) {
        return res;
    }

    size_t data_size = qemu_hypertrace_num_args() * sizeof(uint64_t);
    data_size *= qemu_hypertrace_max_offset() + 1;
    res = init_channel_file(base, data_suff, data_size,
                            &data_path, &data_fd, &data_addr);
    if (res != 0) {
        return res;
    }
    return 0;
}


static int fini_channel(int *fd, char **path)
{
    if (*fd != -1) {
        if (close(*fd) == -1) {
            return -1;
        }
        *fd = -1;
    }
    if (*path != NULL) {
        free(*path);
        *path =  NULL;
    }
    return 0;
}

int qemu_hypertrace_fini(void)
{
    if (fini_channel(&data_fd, &data_path) != 0) {
        return -1;
    }
    if (fini_channel(&control_fd, &control_path) != 0) {
        return -1;
    }
    return 0;
}


uint64_t qemu_hypertrace_num_args(void)
{
    return CONFIG_HYPERTRACE_ARGS;
}

uint64_t qemu_hypertrace_max_offset(void)
{
    return control_addr[0];
}

uint64_t *qemu_hypertrace_data(uint64_t data_offset)
{
    return &data_addr[data_offset * CONFIG_HYPERTRACE_ARGS * sizeof(uint64_t)];
}

void qemu_hypertrace (uint64_t data_offset)
{
    uint64_t *ctrl;
    ctrl = control_addr;
    ctrl[1] = data_offset;
#if defined(CONFIG_USER_ONLY)
    /* QEMU in 'user' mode uses two faulting pages to detect invocations */
    ctrl = (uint64_t*)((char*)control_addr + getpagesize());
    ctrl[1] = data_offset;
#endif
}
