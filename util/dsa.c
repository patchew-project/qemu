/*
 * Use Intel Data Streaming Accelerator to offload certain background
 * operations.
 *
 * Copyright (c) 2023 Hao Xiang <hao.xiang@bytedance.com>
 *                    Bryan Zhang <bryan.zhang@bytedance.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "qemu/memalign.h"
#include "qemu/lockable.h"
#include "qemu/cutils.h"
#include "qemu/dsa.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"

#ifdef CONFIG_DSA_OPT

#pragma GCC push_options
#pragma GCC target("enqcmd")

#include <linux/idxd.h>
#include "x86intrin.h"

#define DSA_WQ_SIZE 4096
#define MAX_DSA_DEVICES 16

typedef QSIMPLEQ_HEAD(dsa_task_queue, buffer_zero_batch_task) dsa_task_queue;

struct dsa_device {
    void *work_queue;
};

struct dsa_device_group {
    struct dsa_device *dsa_devices;
    int num_dsa_devices;
    uint32_t index;
    bool running;
    QemuMutex task_queue_lock;
    QemuCond task_queue_cond;
    dsa_task_queue task_queue;
};

uint64_t max_retry_count;
static struct dsa_device_group dsa_group;


/**
 * @brief This function opens a DSA device's work queue and
 *        maps the DSA device memory into the current process.
 *
 * @param dsa_wq_path A pointer to the DSA device work queue's file path.
 * @return A pointer to the mapped memory.
 */
static void *
map_dsa_device(const char *dsa_wq_path)
{
    void *dsa_device;
    int fd;

    fd = open(dsa_wq_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open %s failed with errno = %d.\n",
                dsa_wq_path, errno);
        return MAP_FAILED;
    }
    dsa_device = mmap(NULL, DSA_WQ_SIZE, PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, fd, 0);
    close(fd);
    if (dsa_device == MAP_FAILED) {
        fprintf(stderr, "mmap failed with errno = %d.\n", errno);
        return MAP_FAILED;
    }
    return dsa_device;
}

/**
 * @brief Initializes a DSA device structure.
 *
 * @param instance A pointer to the DSA device.
 * @param work_queue  A pointer to the DSA work queue.
 */
static void
dsa_device_init(struct dsa_device *instance,
                void *dsa_work_queue)
{
    instance->work_queue = dsa_work_queue;
}

/**
 * @brief Cleans up a DSA device structure.
 *
 * @param instance A pointer to the DSA device to cleanup.
 */
static void
dsa_device_cleanup(struct dsa_device *instance)
{
    if (instance->work_queue != MAP_FAILED) {
        munmap(instance->work_queue, DSA_WQ_SIZE);
    }
}

/**
 * @brief Initializes a DSA device group.
 *
 * @param group A pointer to the DSA device group.
 * @param num_dsa_devices The number of DSA devices this group will have.
 *
 * @return Zero if successful, non-zero otherwise.
 */
static int
dsa_device_group_init(struct dsa_device_group *group,
                      const char *dsa_parameter)
{
    if (dsa_parameter == NULL || strlen(dsa_parameter) == 0) {
        return 0;
    }

    int ret = 0;
    char *local_dsa_parameter = g_strdup(dsa_parameter);
    const char *dsa_path[MAX_DSA_DEVICES];
    int num_dsa_devices = 0;
    char delim[2] = " ";

    char *current_dsa_path = strtok(local_dsa_parameter, delim);

    while (current_dsa_path != NULL) {
        dsa_path[num_dsa_devices++] = current_dsa_path;
        if (num_dsa_devices == MAX_DSA_DEVICES) {
            break;
        }
        current_dsa_path = strtok(NULL, delim);
    }

    group->dsa_devices =
        malloc(sizeof(struct dsa_device) * num_dsa_devices);
    group->num_dsa_devices = num_dsa_devices;
    group->index = 0;

    group->running = false;
    qemu_mutex_init(&group->task_queue_lock);
    qemu_cond_init(&group->task_queue_cond);
    QSIMPLEQ_INIT(&group->task_queue);

    void *dsa_wq = MAP_FAILED;
    for (int i = 0; i < num_dsa_devices; i++) {
        dsa_wq = map_dsa_device(dsa_path[i]);
        if (dsa_wq == MAP_FAILED) {
            fprintf(stderr, "map_dsa_device failed MAP_FAILED, "
                    "using simulation.\n");
            ret = -1;
            goto exit;
        }
        dsa_device_init(&dsa_group.dsa_devices[i], dsa_wq);
    }

exit:
    g_free(local_dsa_parameter);
    return ret;
}

/**
 * @brief Starts a DSA device group.
 *
 * @param group A pointer to the DSA device group.
 * @param dsa_path An array of DSA device path.
 * @param num_dsa_devices The number of DSA devices in the device group.
 */
static void
dsa_device_group_start(struct dsa_device_group *group)
{
    group->running = true;
}

/**
 * @brief Stops a DSA device group.
 *
 * @param group A pointer to the DSA device group.
 */
__attribute__((unused))
static void
dsa_device_group_stop(struct dsa_device_group *group)
{
    group->running = false;
}

/**
 * @brief Cleans up a DSA device group.
 *
 * @param group A pointer to the DSA device group.
 */
static void
dsa_device_group_cleanup(struct dsa_device_group *group)
{
    if (!group->dsa_devices) {
        return;
    }
    for (int i = 0; i < group->num_dsa_devices; i++) {
        dsa_device_cleanup(&group->dsa_devices[i]);
    }
    free(group->dsa_devices);
    group->dsa_devices = NULL;

    qemu_mutex_destroy(&group->task_queue_lock);
    qemu_cond_destroy(&group->task_queue_cond);
}

/**
 * @brief Returns the next available DSA device in the group.
 *
 * @param group A pointer to the DSA device group.
 *
 * @return struct dsa_device* A pointer to the next available DSA device
 *         in the group.
 */
__attribute__((unused))
static struct dsa_device *
dsa_device_group_get_next_device(struct dsa_device_group *group)
{
    if (group->num_dsa_devices == 0) {
        return NULL;
    }
    uint32_t current = qatomic_fetch_inc(&group->index);
    current %= group->num_dsa_devices;
    return &group->dsa_devices[current];
}

/**
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool dsa_is_running(void)
{
    return false;
}

static void
dsa_globals_init(void)
{
    max_retry_count = UINT64_MAX;
}

/**
 * @brief Initializes DSA devices.
 *
 * @param dsa_parameter A list of DSA device path from migration parameter.
 * @return int Zero if successful, otherwise non zero.
 */
int dsa_init(const char *dsa_parameter)
{
    dsa_globals_init();

    return dsa_device_group_init(&dsa_group, dsa_parameter);
}

/**
 * @brief Start logic to enable using DSA.
 *
 */
void dsa_start(void)
{
    if (dsa_group.num_dsa_devices == 0) {
        return;
    }
    if (dsa_group.running) {
        return;
    }
    dsa_device_group_start(&dsa_group);
}

/**
 * @brief Stop logic to clean up DSA by halting the device group and cleaning up
 * the completion thread.
 *
 */
void dsa_stop(void)
{
    struct dsa_device_group *group = &dsa_group;

    if (!group->running) {
        return;
    }
}

/**
 * @brief Clean up system resources created for DSA offloading.
 *        This function is called during QEMU process teardown.
 *
 */
void dsa_cleanup(void)
{
    dsa_stop();
    dsa_device_group_cleanup(&dsa_group);
}

#else

bool dsa_is_running(void)
{
    return false;
}

int dsa_init(const char *dsa_parameter)
{
    fprintf(stderr, "Intel Data Streaming Accelerator is not supported "
                    "on this platform.\n");
    return -1;
}

void dsa_start(void) {}

void dsa_stop(void) {}

void dsa_cleanup(void) {}

#endif

