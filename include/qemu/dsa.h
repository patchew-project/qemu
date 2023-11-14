#ifndef QEMU_DSA_H
#define QEMU_DSA_H

#include "qemu/thread.h"
#include "qemu/queue.h"

#ifdef CONFIG_DSA_OPT

#pragma GCC push_options
#pragma GCC target("enqcmd")

#include <linux/idxd.h>
#include "x86intrin.h"

enum dsa_task_type {
    DSA_TASK = 0,
    DSA_BATCH_TASK
};

enum dsa_task_status {
    DSA_TASK_READY = 0,
    DSA_TASK_PROCESSING,
    DSA_TASK_COMPLETION
};

typedef void (*buffer_zero_dsa_completion_fn)(void *);

typedef struct buffer_zero_batch_task {
    struct dsa_hw_desc batch_descriptor;
    struct dsa_hw_desc *descriptors;
    struct dsa_completion_record batch_completion __attribute__((aligned(32)));
    struct dsa_completion_record *completions;
    struct dsa_device_group *group;
    struct dsa_device *device;
    buffer_zero_dsa_completion_fn completion_callback;
    QemuSemaphore sem_task_complete;
    enum dsa_task_type task_type;
    enum dsa_task_status status;
    bool *results;
    int batch_size;
    QSIMPLEQ_ENTRY(buffer_zero_batch_task) entry;
} buffer_zero_batch_task;

#else

struct buffer_zero_batch_task {
    bool *results;
};

#endif

/**
 * @brief Initializes DSA devices.
 *
 * @param dsa_parameter A list of DSA device path from migration parameter.
 * @return int Zero if successful, otherwise non zero.
 */
int dsa_init(const char *dsa_parameter);

/**
 * @brief Start logic to enable using DSA.
 */
void dsa_start(void);

/**
 * @brief Stop logic to clean up DSA by halting the device group and cleaning up
 * the completion thread.
 */
void dsa_stop(void);

/**
 * @brief Clean up system resources created for DSA offloading.
 *        This function is called during QEMU process teardown.
 */
void dsa_cleanup(void);

/**
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool dsa_is_running(void);

#endif