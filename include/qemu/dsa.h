#ifndef QEMU_DSA_H
#define QEMU_DSA_H

#include "qemu/error-report.h"
#include "qemu/thread.h"
#include "qemu/queue.h"

#ifdef CONFIG_DSA_OPT

#pragma GCC push_options
#pragma GCC target("enqcmd")

#include <linux/idxd.h>
#include "x86intrin.h"

typedef enum DsaTaskType {
    DSA_TASK = 0,
    DSA_BATCH_TASK
} DsaTaskType;

typedef enum DsaTaskStatus {
    DSA_TASK_READY = 0,
    DSA_TASK_PROCESSING,
    DSA_TASK_COMPLETION
} DsaTaskStatus;

typedef void (*dsa_completion_fn)(void *);

typedef struct dsa_batch_task {
    struct dsa_hw_desc batch_descriptor;
    struct dsa_hw_desc *descriptors;
    struct dsa_completion_record batch_completion __attribute__((aligned(32)));
    struct dsa_completion_record *completions;
    struct dsa_device_group *group;
    struct dsa_device *device;
    dsa_completion_fn completion_callback;
    QemuSemaphore sem_task_complete;
    DsaTaskType task_type;
    DsaTaskStatus status;
    int batch_size;
    QSIMPLEQ_ENTRY(dsa_batch_task) entry;
} dsa_batch_task;

/**
 * @brief Initializes DSA devices.
 *
 * @param dsa_parameter A list of DSA device path from migration parameter.
 *
 * @return int Zero if successful, otherwise non zero.
 */
int dsa_init(const char *dsa_parameter);

/**
 * @brief Start logic to enable using DSA.
 */
void dsa_start(void);

/**
 * @brief Stop the device group and the completion thread.
 */
void dsa_stop(void);

/**
 * @brief Clean up system resources created for DSA offloading.
 */
void dsa_cleanup(void);

/**
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool dsa_is_running(void);

#else

static inline bool dsa_is_running(void)
{
    return false;
}

static inline int dsa_init(const char *dsa_parameter)
{
    if (dsa_parameter != NULL && strlen(dsa_parameter) != 0) {
        error_report("DSA not supported.");
        return -1;
    }

    return 0;
}

static inline void dsa_start(void) {}

static inline void dsa_stop(void) {}

static inline void dsa_cleanup(void) {}

#endif

#endif
