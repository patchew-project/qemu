#ifndef QEMU_DSA_H
#define QEMU_DSA_H

#include "qemu/error-report.h"
#include "exec/cpu-common.h"
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
    bool *results;
    QSIMPLEQ_ENTRY(dsa_batch_task) entry;
} dsa_batch_task;

#endif

struct batch_task {
#ifdef CONFIG_DSA_OPT
    /* Address of each pages in pages */
    ram_addr_t *addr;
    /* Zero page checking results */
    bool *results;
    /* Batch task DSA specific implementation */
    struct dsa_batch_task *dsa_batch;
#endif
};

#ifdef CONFIG_DSA_OPT

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

/**
 * @brief Initializes a buffer zero DSA batch task.
 *
 * @param task A pointer to the batch task to initialize.
 * @param results A pointer to an array of zero page checking results.
 * @param batch_size The number of DSA tasks in the batch.
 */
void
buffer_zero_batch_task_init(struct dsa_batch_task *task,
                            bool *results, int batch_size);

/**
 * @brief Performs the proper cleanup on a DSA batch task.
 *
 * @param task A pointer to the batch task to cleanup.
 */
void buffer_zero_batch_task_destroy(struct dsa_batch_task *task);

/**
 * @brief Performs buffer zero comparison on a DSA batch task asynchronously.
 *
 * @param batch_task A pointer to the batch task.
 * @param buf An array of memory buffers.
 * @param count The number of buffers in the array.
 * @param len The buffer length.
 *
 * @return Zero if successful, otherwise non-zero.
 */
int
buffer_is_zero_dsa_batch_async(struct batch_task *batch_task,
                               const void **buf, size_t count, size_t len);

/**
 * @brief Initializes a general buffer zero batch task.
 *
 * @param batch_size The number of zero page checking tasks in the batch.
 * @return A pointer to the general batch task initialized.
 */
struct batch_task *
batch_task_init(int batch_size);

/**
 * @brief Destroys a general buffer zero batch task.
 *
 * @param task A pointer to the general batch task to destroy.
 */
void
batch_task_destroy(struct batch_task *task);

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

static inline int
buffer_is_zero_dsa_batch_async(struct batch_task *batch_task,
                               const void **buf, size_t count, size_t len)
{
    exit(1);
}

static inline struct batch_task *batch_task_init(int batch_size)
{
    return NULL;
}

static inline void batch_task_destroy(struct batch_task *task) {}

#endif

#endif
