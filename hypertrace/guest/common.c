/*
 * Guest-side management of hypertrace.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu-hypertrace.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
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
#include "hypertrace/common.h"

static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *config_path;
static int config_fd = -1;
static uint64_t *config_addr;
static struct hypertrace_config *config;

static char *data_path;
static int data_fd = -1;
static uint64_t *data_addr;

static char *control_path;
static int control_fd = -1;
#if defined(CONFIG_USER_ONLY)
static __thread uint64_t *control_addr;
static __thread uint64_t *control_addr_1;
#else
static uint64_t *control_addr;
#endif

static int page_size;


static int init_channel_file(const char *base, const char *suffix, size_t size,
                             char **path, int *fd, uint64_t **addr, bool write)
{
    int prot;

    *path = malloc(strlen(base) + strlen(suffix) + 1);
    sprintf(*path, "%s%s", base, suffix);

    prot = O_RDONLY;
    if (write) {
        prot = O_RDWR;
    }
    *fd = open(*path, prot);
    if (*fd == -1) {
        return -1;
    }

    prot = PROT_READ;
    if (write) {
        prot |= PROT_WRITE;
    }
    *addr = mmap(NULL, size, prot, MAP_SHARED, *fd, 0);
    if (*addr == MAP_FAILED) {
        return -1;
    }
    return 0;
}

#if !defined(CONFIG_USER_ONLY) && defined(__linux__)
static int check_device_id(const char *base, const char *name, uint64_t value)
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

    char *res;
    uint64_t vv = strtoull(v, &res, 16);
    if (*res == '\n' && vv == value) {
        return 0;
    } else {
        return -1;
    }
}

static char *find_device(void)
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

        if (check_device_id(path, "vendor",
                            PCI_VENDOR_ID_REDHAT_QUMRANET) < 0) {
            continue;
        }
        if (check_device_id(path, "device",
                            PCI_DEVICE_ID_HYPERTRACE) < 0) {
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
    const char *config_suff = "-config";
    const char *data_suff = "-data";
    const char *control_suff = "-control";
#elif defined(__linux__)
    const char *config_suff = "/resource0";
    const char *data_suff = "/resource1";
    const char *control_suff = "/resource2";
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

    if (config_addr == NULL) {
        int res;

        if (pthread_mutex_lock(&init_mutex)) {
            return -1;
        }

        page_size = getpagesize();

        res = init_channel_file(base, config_suff, page_size,
                                &config_path, &config_fd, &config_addr,
                                false);
        if (res != 0) {
            return res;
        }

        config = (struct hypertrace_config *)config_addr;

        if (pthread_mutex_unlock(&init_mutex)) {
            return -1;
        }
    }

    if (data_addr == NULL) {
        int res;

        if (pthread_mutex_lock(&init_mutex)) {
            return -1;
        }

        res = init_channel_file(base, data_suff, config->data_size,
                                &data_path, &data_fd, &data_addr,
                                true);
        if (res != 0) {
            return res;
        }

        if (pthread_mutex_unlock(&init_mutex)) {
            return -1;
        }
    }

    if (control_addr == NULL) {
        int res;
        uint64_t control_size = config->control_size;

        if (pthread_mutex_lock(&init_mutex)) {
            return -1;
        }

        res = init_channel_file(base, control_suff, control_size,
                                &control_path, &control_fd, &control_addr,
                                true);
        if (res != 0) {
            return res;
        }

#if defined(CONFIG_USER_ONLY)
        control_addr_1 = (uint64_t *)((char *)control_addr +
                                      config->control_size / 2);
#endif

        if (pthread_mutex_unlock(&init_mutex)) {
            return -1;
        }
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


uint64_t qemu_hypertrace_max_clients(void)
{
    return config->max_clients;
}

uint64_t qemu_hypertrace_num_args(void)
{
    return config->client_args;
}

uint64_t *qemu_hypertrace_data(uint64_t client)
{
    return &data_addr[client * CONFIG_HYPERTRACE_ARGS * sizeof(uint64_t)];
}

void qemu_hypertrace(uint64_t client, uint64_t arg1)
{
    uint64_t offset;
#if defined(CONFIG_USER_ONLY)
    offset = client * page_size;
#endif
    *(uint64_t *)((char *)control_addr + offset) = arg1;
#if defined(CONFIG_USER_ONLY)
    /* QEMU in 'user' mode uses two faulting pages to detect invocations */
    *(uint64_t *)((char *)control_addr_1 + offset) = arg1;
#endif
}
