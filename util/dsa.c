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
#define DSA_COMPLETION_THREAD "dsa_completion"

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

struct dsa_completion_thread {
    bool stopping;
    bool running;
    QemuThread thread;
    int thread_id;
    QemuSemaphore sem_init_done;
    struct dsa_device_group *group;
};

uint64_t max_retry_count;
static struct dsa_device_group dsa_group;
static struct dsa_completion_thread completion_thread;

static void buffer_zero_dsa_completion(void *context);

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
 * @brief Empties out the DSA task queue.
 *
 * @param group A pointer to the DSA device group.
 */
static void
dsa_empty_task_queue(struct dsa_device_group *group)
{
    qemu_mutex_lock(&group->task_queue_lock);
    dsa_task_queue *task_queue = &group->task_queue;
    while (!QSIMPLEQ_EMPTY(task_queue)) {
        QSIMPLEQ_REMOVE_HEAD(task_queue, entry);
    }
    qemu_mutex_unlock(&group->task_queue_lock);
}

/**
 * @brief Adds a task to the DSA task queue.
 *
 * @param group A pointer to the DSA device group.
 * @param context A pointer to the DSA task to enqueue.
 *
 * @return int Zero if successful, otherwise a proper error code.
 */
static int
dsa_task_enqueue(struct dsa_device_group *group,
                 struct buffer_zero_batch_task *task)
{
    dsa_task_queue *task_queue = &group->task_queue;
    QemuMutex *task_queue_lock = &group->task_queue_lock;
    QemuCond *task_queue_cond = &group->task_queue_cond;

    bool notify = false;

    qemu_mutex_lock(task_queue_lock);

    if (!group->running) {
        fprintf(stderr, "DSA: Tried to queue task to stopped device queue\n");
        qemu_mutex_unlock(task_queue_lock);
        return -1;
    }

    // The queue is empty. This enqueue operation is a 0->1 transition.
    if (QSIMPLEQ_EMPTY(task_queue))
        notify = true;

    QSIMPLEQ_INSERT_TAIL(task_queue, task, entry);

    // We need to notify the waiter for 0->1 transitions.
    if (notify)
        qemu_cond_signal(task_queue_cond);

    qemu_mutex_unlock(task_queue_lock);

    return 0;
}

/**
 * @brief Takes a DSA task out of the task queue.
 *
 * @param group A pointer to the DSA device group.
 * @return buffer_zero_batch_task* The DSA task being dequeued.
 */
static struct buffer_zero_batch_task *
dsa_task_dequeue(struct dsa_device_group *group)
{
    struct buffer_zero_batch_task *task = NULL;
    dsa_task_queue *task_queue = &group->task_queue;
    QemuMutex *task_queue_lock = &group->task_queue_lock;
    QemuCond *task_queue_cond = &group->task_queue_cond;

    qemu_mutex_lock(task_queue_lock);

    while (true) {
        if (!group->running)
            goto exit;
        task = QSIMPLEQ_FIRST(task_queue);
        if (task != NULL) {
            break;
        }
        qemu_cond_wait(task_queue_cond, task_queue_lock);
    }

    QSIMPLEQ_REMOVE_HEAD(task_queue, entry);

exit:
    qemu_mutex_unlock(task_queue_lock);
    return task;
}

/**
 * @brief Submits a DSA work item to the device work queue.
 *
 * @param wq A pointer to the DSA work queue's device memory.
 * @param descriptor A pointer to the DSA work item descriptor.
 *
 * @return Zero if successful, non-zero otherwise.
 */
static int
submit_wi_int(void *wq, struct dsa_hw_desc *descriptor)
{
    uint64_t retry = 0;

    _mm_sfence();

    while (true) {
        if (_enqcmd(wq, descriptor) == 0) {
            break;
        }
        retry++;
        if (retry > max_retry_count) {
            fprintf(stderr, "Submit work retry %lu times.\n", retry);
            exit(1);
        }
    }

    return 0;
}

/**
 * @brief Asynchronously submits a DSA work item to the
 *        device work queue.
 *
 * @param task A pointer to the buffer zero task.
 *
 * @return int Zero if successful, non-zero otherwise.
 */
static int
submit_wi_async(struct buffer_zero_batch_task *task)
{
    struct dsa_device_group *device_group = task->group;
    struct dsa_device *device_instance = task->device;
    int ret;

    assert(task->task_type == DSA_TASK);

    task->status = DSA_TASK_PROCESSING;

    ret = submit_wi_int(device_instance->work_queue,
                        &task->descriptors[0]);
    if (ret != 0)
        return ret;

    return dsa_task_enqueue(device_group, task);
}

/**
 * @brief Asynchronously submits a DSA batch work item to the
 *        device work queue.
 *
 * @param batch_task A pointer to the batch buffer zero task.
 *
 * @return int Zero if successful, non-zero otherwise.
 */
static int
submit_batch_wi_async(struct buffer_zero_batch_task *batch_task)
{
    struct dsa_device_group *device_group = batch_task->group;
    struct dsa_device *device_instance = batch_task->device;
    int ret;

    assert(batch_task->task_type == DSA_BATCH_TASK);
    assert(batch_task->batch_descriptor.desc_count <= batch_task->batch_size);
    assert(batch_task->status == DSA_TASK_READY);

    batch_task->status = DSA_TASK_PROCESSING;

    ret = submit_wi_int(device_instance->work_queue,
                        &batch_task->batch_descriptor);
    if (ret != 0)
        return ret;

    return dsa_task_enqueue(device_group, batch_task);
}

/**
 * @brief Poll for the DSA work item completion.
 *
 * @param completion A pointer to the DSA work item completion record.
 * @param opcode The DSA opcode.
 *
 * @return Zero if successful, non-zero otherwise.
 */
static int
poll_completion(struct dsa_completion_record *completion,
                enum dsa_opcode opcode)
{
    uint8_t status;
    uint64_t retry = 0;

    while (true) {
        // The DSA operation completes successfully or fails.
        status = completion->status;
        if (status == DSA_COMP_SUCCESS ||
            status == DSA_COMP_PAGE_FAULT_NOBOF ||
            status == DSA_COMP_BATCH_PAGE_FAULT ||
            status == DSA_COMP_BATCH_FAIL) {
            break;
        } else if (status != DSA_COMP_NONE) {
            /* TODO: Error handling here on unexpected failure. */
            fprintf(stderr, "DSA opcode %d failed with status = %d.\n",
                    opcode, status);
            exit(1);
        }
        retry++;
        if (retry > max_retry_count) {
            fprintf(stderr, "Wait for completion retry %lu times.\n", retry);
            exit(1);
        }
        _mm_pause();
    }

    return 0;
}

/**
 * @brief Complete a single DSA task in the batch task.
 *
 * @param task A pointer to the batch task structure.
 */
static void
poll_task_completion(struct buffer_zero_batch_task *task)
{
    assert(task->task_type == DSA_TASK);

    struct dsa_completion_record *completion = &task->completions[0];
    uint8_t status;

    poll_completion(completion, task->descriptors[0].opcode);

    status = completion->status;
    if (status == DSA_COMP_SUCCESS) {
        task->results[0] = (completion->result == 0);
        return;
    }

    assert(status == DSA_COMP_PAGE_FAULT_NOBOF);
}

/**
 * @brief Poll a batch task status until it completes. If DSA task doesn't
 *        complete properly, use CPU to complete the task.
 *
 * @param batch_task A pointer to the DSA batch task.
 */
static void
poll_batch_task_completion(struct buffer_zero_batch_task *batch_task)
{
    struct dsa_completion_record *batch_completion = &batch_task->batch_completion;
    struct dsa_completion_record *completion;
    uint8_t batch_status;
    uint8_t status;
    bool *results = batch_task->results;
    uint32_t count = batch_task->batch_descriptor.desc_count;

    poll_completion(batch_completion,
                    batch_task->batch_descriptor.opcode);

    batch_status = batch_completion->status;

    if (batch_status == DSA_COMP_SUCCESS) {
        if (batch_completion->bytes_completed == count) {
            // Let's skip checking for each descriptors' completion status
            // if the batch descriptor says all succedded.
            for (int i = 0; i < count; i++) {
                assert(batch_task->completions[i].status == DSA_COMP_SUCCESS);
                results[i] = (batch_task->completions[i].result == 0);
            }
            return;
        }
    } else {
        assert(batch_status == DSA_COMP_BATCH_FAIL ||
            batch_status == DSA_COMP_BATCH_PAGE_FAULT);
    }

    for (int i = 0; i < count; i++) {

        completion = &batch_task->completions[i];
        status = completion->status;

        if (status == DSA_COMP_SUCCESS) {
            results[i] = (completion->result == 0);
            continue;
        }

        if (status != DSA_COMP_PAGE_FAULT_NOBOF) {
            fprintf(stderr,
                    "Unexpected completion status = %u.\n", status);
            assert(false);
        }
    }
}

/**
 * @brief Handles an asynchronous DSA batch task completion.
 *
 * @param task A pointer to the batch buffer zero task structure.
 */
static void
dsa_batch_task_complete(struct buffer_zero_batch_task *batch_task)
{
    batch_task->status = DSA_TASK_COMPLETION;
    batch_task->completion_callback(batch_task);
}

/**
 * @brief The function entry point called by a dedicated DSA
 *        work item completion thread.
 *
 * @param opaque A pointer to the thread context.
 *
 * @return void* Not used.
 */
static void *
dsa_completion_loop(void *opaque)
{
    struct dsa_completion_thread *thread_context =
        (struct dsa_completion_thread *)opaque;
    struct buffer_zero_batch_task *batch_task;
    struct dsa_device_group *group = thread_context->group;

    rcu_register_thread();

    thread_context->thread_id = qemu_get_thread_id();
    qemu_sem_post(&thread_context->sem_init_done);

    while (thread_context->running) {
        batch_task = dsa_task_dequeue(group);
        assert(batch_task != NULL || !group->running);
        if (!group->running) {
            assert(!thread_context->running);
            break;
        }
        if (batch_task->task_type == DSA_TASK) {
            poll_task_completion(batch_task);
        } else {
            assert(batch_task->task_type == DSA_BATCH_TASK);
            poll_batch_task_completion(batch_task);
        }

        dsa_batch_task_complete(batch_task);
    }

    rcu_unregister_thread();
    return NULL;
}

/**
 * @brief Initializes a DSA completion thread.
 *
 * @param completion_thread A pointer to the completion thread context.
 * @param group A pointer to the DSA device group.
 */
static void
dsa_completion_thread_init(
    struct dsa_completion_thread *completion_thread,
    struct dsa_device_group *group)
{
    completion_thread->stopping = false;
    completion_thread->running = true;
    completion_thread->thread_id = -1;
    qemu_sem_init(&completion_thread->sem_init_done, 0);
    completion_thread->group = group;

    qemu_thread_create(&completion_thread->thread,
                       DSA_COMPLETION_THREAD,
                       dsa_completion_loop,
                       completion_thread,
                       QEMU_THREAD_JOINABLE);

    /* Wait for initialization to complete */
    while (completion_thread->thread_id == -1) {
        qemu_sem_wait(&completion_thread->sem_init_done);
    }
}

/**
 * @brief Stops the completion thread (and implicitly, the device group).
 *
 * @param opaque A pointer to the completion thread.
 */
static void dsa_completion_thread_stop(void *opaque)
{
    struct dsa_completion_thread *thread_context =
        (struct dsa_completion_thread *)opaque;

    struct dsa_device_group *group = thread_context->group;

    qemu_mutex_lock(&group->task_queue_lock);

    thread_context->stopping = true;
    thread_context->running = false;

    dsa_device_group_stop(group);

    qemu_cond_signal(&group->task_queue_cond);
    qemu_mutex_unlock(&group->task_queue_lock);

    qemu_thread_join(&thread_context->thread);

    qemu_sem_destroy(&thread_context->sem_init_done);
}

/**
 * @brief Initializes a buffer zero comparison DSA task.
 *
 * @param descriptor A pointer to the DSA task descriptor.
 * @param completion A pointer to the DSA task completion record.
 */
static void
buffer_zero_task_init_int(struct dsa_hw_desc *descriptor,
                          struct dsa_completion_record *completion)
{
    descriptor->opcode = DSA_OPCODE_COMPVAL;
    descriptor->flags = IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV;
    descriptor->comp_pattern = (uint64_t)0;
    descriptor->completion_addr = (uint64_t)completion;
}

/**
 * @brief Initializes a buffer zero batch task.
 *
 * @param task A pointer to the batch task to initialize.
 * @param batch_size The number of DSA tasks in the batch.
 */
void
buffer_zero_batch_task_init(struct buffer_zero_batch_task *task,
                            int batch_size)
{
    int descriptors_size = sizeof(*task->descriptors) * batch_size;
    memset(task, 0, sizeof(*task));

    task->descriptors =
        (struct dsa_hw_desc *)qemu_memalign(64, descriptors_size);
    memset(task->descriptors, 0, descriptors_size);
    task->completions = (struct dsa_completion_record *)qemu_memalign(
        32, sizeof(*task->completions) * batch_size);
    task->results = g_new0(bool, batch_size);
    task->batch_size = batch_size;

    task->batch_completion.status = DSA_COMP_NONE;
    task->batch_descriptor.completion_addr = (uint64_t)&task->batch_completion;
    // TODO: Ensure that we never send a batch with count <= 1
    task->batch_descriptor.desc_count = 0;
    task->batch_descriptor.opcode = DSA_OPCODE_BATCH;
    task->batch_descriptor.flags = IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV;
    task->batch_descriptor.desc_list_addr = (uintptr_t)task->descriptors;
    task->status = DSA_TASK_READY;
    task->group = &dsa_group;
    task->device = dsa_device_group_get_next_device(&dsa_group);

    for (int i = 0; i < task->batch_size; i++) {
        buffer_zero_task_init_int(&task->descriptors[i],
                                  &task->completions[i]);
    }

    qemu_sem_init(&task->sem_task_complete, 0);
    task->completion_callback = buffer_zero_dsa_completion;
}

/**
 * @brief Performs the proper cleanup on a DSA batch task.
 *
 * @param task A pointer to the batch task to cleanup.
 */
void
buffer_zero_batch_task_destroy(struct buffer_zero_batch_task *task)
{
    qemu_vfree(task->descriptors);
    qemu_vfree(task->completions);
    g_free(task->results);

    qemu_sem_destroy(&task->sem_task_complete);
}

/**
 * @brief Resets a buffer zero comparison DSA batch task.
 *
 * @param task A pointer to the batch task.
 * @param count The number of DSA tasks this batch task will contain.
 */
static void
buffer_zero_batch_task_reset(struct buffer_zero_batch_task *task, size_t count)
{
    task->batch_completion.status = DSA_COMP_NONE;
    task->batch_descriptor.desc_count = count;
    task->task_type = DSA_BATCH_TASK;
    task->status = DSA_TASK_READY;
}

/**
 * @brief Sets a buffer zero comparison DSA task.
 *
 * @param descriptor A pointer to the DSA task descriptor.
 * @param buf A pointer to the memory buffer.
 * @param len The length of the buffer.
 */
static void
buffer_zero_task_set_int(struct dsa_hw_desc *descriptor,
                         const void *buf,
                         size_t len)
{
    struct dsa_completion_record *completion =
        (struct dsa_completion_record *)descriptor->completion_addr;

    descriptor->xfer_size = len;
    descriptor->src_addr = (uintptr_t)buf;
    completion->status = 0;
    completion->result = 0;
}

/**
 * @brief Resets a buffer zero comparison DSA batch task.
 *
 * @param task A pointer to the DSA batch task.
 */
static void
buffer_zero_task_reset(struct buffer_zero_batch_task *task)
{
    task->completions[0].status = DSA_COMP_NONE;
    task->task_type = DSA_TASK;
    task->status = DSA_TASK_READY;
}

/**
 * @brief Sets a buffer zero comparison DSA task.
 *
 * @param task A pointer to the DSA task.
 * @param buf A pointer to the memory buffer.
 * @param len The buffer length.
 */
static void
buffer_zero_task_set(struct buffer_zero_batch_task *task,
                     const void *buf,
                     size_t len)
{
    buffer_zero_task_reset(task);
    buffer_zero_task_set_int(&task->descriptors[0], buf, len);
}

/**
 * @brief Sets a buffer zero comparison batch task.
 *
 * @param batch_task A pointer to the batch task.
 * @param buf An array of memory buffers.
 * @param count The number of buffers in the array.
 * @param len The length of the buffers.
 */
static void
buffer_zero_batch_task_set(struct buffer_zero_batch_task *batch_task,
                           const void **buf, size_t count, size_t len)
{
    assert(count > 0);
    assert(count <= batch_task->batch_size);

    buffer_zero_batch_task_reset(batch_task, count);
    for (int i = 0; i < count; i++) {
        buffer_zero_task_set_int(&batch_task->descriptors[i], buf[i], len);
    }
}

/**
 * @brief Asychronously perform a buffer zero DSA operation.
 *
 * @param task A pointer to the batch task structure.
 * @param buf A pointer to the memory buffer.
 * @param len The length of the memory buffer.
 *
 * @return int Zero if successful, otherwise an appropriate error code.
 */
__attribute__((unused))
static int
buffer_zero_dsa_async(struct buffer_zero_batch_task *task,
                      const void *buf, size_t len)
{
    buffer_zero_task_set(task, buf, len);

    return submit_wi_async(task);
}

/**
 * @brief Sends a memory comparison batch task to a DSA device and wait
 *        for completion.
 *
 * @param batch_task The batch task to be submitted to DSA device.
 * @param buf An array of memory buffers to check for zero.
 * @param count The number of buffers.
 * @param len The buffer length.
 */
__attribute__((unused))
static int
buffer_zero_dsa_batch_async(struct buffer_zero_batch_task *batch_task,
                            const void **buf, size_t count, size_t len)
{
    assert(count <= batch_task->batch_size);
    buffer_zero_batch_task_set(batch_task, buf, count, len);

    return submit_batch_wi_async(batch_task);
}

/**
 * @brief The completion callback function for buffer zero
 *        comparison DSA task completion.
 *
 * @param context A pointer to the callback context.
 */
static void
buffer_zero_dsa_completion(void *context)
{
    assert(context != NULL);

    struct buffer_zero_batch_task *task =
        (struct buffer_zero_batch_task *)context;
    qemu_sem_post(&task->sem_task_complete);
}

/**
 * @brief Wait for the asynchronous DSA task to complete.
 *
 * @param batch_task A pointer to the buffer zero comparison batch task.
 */
__attribute__((unused))
static void
buffer_zero_dsa_wait(struct buffer_zero_batch_task *batch_task)
{
    qemu_sem_wait(&batch_task->sem_task_complete);
}

/**
 * @brief Check if DSA is running.
 *
 * @return True if DSA is running, otherwise false.
 */
bool dsa_is_running(void)
{
    return completion_thread.running;
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
    dsa_completion_thread_init(&completion_thread, &dsa_group);
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

    dsa_completion_thread_stop(&completion_thread);
    dsa_empty_task_queue(group);
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

void buffer_zero_batch_task_init(struct buffer_zero_batch_task *task,
                                 int batch_size) {}

void buffer_zero_batch_task_destroy(struct buffer_zero_batch_task *task) {}

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

