/*
 * CPU thread main loop - common bits for user and system mode emulation
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "block/graph-lock.h"
#include "block/block.h"
#include "block/block_int.h"

/* Protects the list of aiocontext and global_reader_count */
static QemuMutex aio_context_list_lock;

/* Written under aio_context_list_lock, read with atomic operations. */
static int has_writer;

/*
 * A reader coroutine could move from an AioContext to another.
 * If this happens, there is no problem from the point of view of
 * counters. The problem is when the original AioContext where it
 * took the rdlock gets deleted. We can't lose the counter of
 * readers that are still running, so transfer it to this
 * global variable.
 * Protected by aio_context_list_lock.
 */
static uint32_t global_reader_count;

/* Queue of readers waiting for the writer to finish */
static CoQueue exclusive_resume;

/*
 * List of AioContext. This list ensures that each AioContext
 * can safely modify only its own counter, avoid reading/writing
 * others and thus improving performances by avoidind cacheline bounces.
 */
static QTAILQ_HEAD(, AioContext) aio_context_list =
    QTAILQ_HEAD_INITIALIZER(aio_context_list);

static void __attribute__((__constructor__)) bdrv_init_graph_lock(void)
{
    qemu_mutex_init(&aio_context_list_lock);
    qemu_co_queue_init(&exclusive_resume);
}

void register_aiocontext(AioContext *ctx)
{
    QEMU_LOCK_GUARD(&aio_context_list_lock);
    assert(ctx->reader_count == 0);
    QTAILQ_INSERT_TAIL(&aio_context_list, ctx, next_aio);
}

void unregister_aiocontext(AioContext *ctx)
{
    QEMU_LOCK_GUARD(&aio_context_list_lock);
    global_reader_count += ctx->reader_count;
    QTAILQ_REMOVE(&aio_context_list, ctx, next_aio);
}

static uint32_t reader_count(void)
{
    AioContext *ctx;
    uint32_t rd;

    QEMU_LOCK_GUARD(&aio_context_list_lock);

    /* rd can temporarly be negative, but the will *always* >= 0 */
    rd = global_reader_count;
    QTAILQ_FOREACH(ctx, &aio_context_list, next_aio) {
        rd += qatomic_read(&ctx->reader_count);
    }

    /* shouldn't overflow unless there are 2^31 readers */
    assert((int32_t)rd >= 0);
    return rd;
}

/*
 * Start an exclusive write operation.
 * Must only be called from outside bdrv_graph_co_rdlock.
 */
void bdrv_graph_wrlock(void)
{
    GLOBAL_STATE_CODE();
    assert(!has_writer);

    qatomic_set(&has_writer, 1);
    /* make sure all readers read has_writer = 1 */
    smp_mb();

    /*
     * reader_count == 0: this means writer will read has_reader as 1
     * reader_count >= 1: we don't know if writer read has_writer == 0 or 1,
     *                    but we need to wait.
     * Wait by allowing other coroutine (and possible readers) to continue.
     */
    AIO_WAIT_WHILE(qemu_get_aio_context(), reader_count() >= 1);
}

/* Finish an exclusive write operation.  */
void bdrv_graph_wrunlock(void)
{
    GLOBAL_STATE_CODE();
    QEMU_LOCK_GUARD(&aio_context_list_lock);
    assert(has_writer);

    /*
     * No need for memory barriers, this works in pair with
     * the slow path of rdlock() and both take the lock.
     */
    qatomic_store_release(&has_writer, 0);

    /* Wake up all coroutine that are waiting to read the graph */
    qemu_co_queue_restart_all_lockable(&exclusive_resume,
                                       &aio_context_list_lock);
}

/* Wait for exclusive write to finish, and begin reading graph.  */
void coroutine_fn bdrv_graph_co_rdlock(void)
{
    AioContext *aiocontext;
    aiocontext = qemu_get_current_aio_context();

    for (;;) {
        qatomic_set(&aiocontext->reader_count,
                    aiocontext->reader_count + 1);
        /* make sure writer sees reader_count */
        smp_mb();

        /*
         * has_writer == 0: this means writer will read reader_count as >= 1
         * has_writer == 1: we don't know if writer read reader_count == 0
         *                  or > 0, but we need to wait anyways because
         *                  it will write.
         */
        if (!qatomic_read(&has_writer)) {
            break;
        }

        /*
         * Synchronize access with reader_count() in bdrv_graph_wrlock().
         * Case 1:
         * If this critical section gets executed first, reader_count will
         * decrease and the reader will go to sleep.
         * Then the writer will read reader_count that does not take into
         * account this reader, and if there's no other reader it will
         * enter the write section.
         * Case 2:
         * If reader_count() critical section gets executed first,
         * then writer will read reader_count >= 1.
         * It will wait in AIO_WAIT_WHILE(), but once it releases the lock
         * we will enter this critical section and call aio_wait_kick().
         */
        WITH_QEMU_LOCK_GUARD(&aio_context_list_lock) {
            /*
             * Additional check when we use the above lock to synchronize
             * with bdrv_graph_wrunlock().
             * Case 1:
             * If this gets executed first, has_writer is still 1, so we reduce
             * reader_count and go to sleep.
             * Then the writer will set has_writer to 0 and wake up all readers,
             * us included.
             * Case 2:
             * If bdrv_graph_wrunlock() critical section gets executed first,
             * then it will set has_writer to 0 and wake up all other readers.
             * Then we execute this critical section, and therefore must check
             * again for has_writer, otherwise we sleep without any writer
             * actually running.
             */
            if (!qatomic_read(&has_writer)) {
                return;
            }

            /* slow path where reader sleeps */
            aiocontext->reader_count--;
            aio_wait_kick();
            qemu_co_queue_wait(&exclusive_resume, &aio_context_list_lock);
        }
    }
}

/* Mark bs as not reading anymore, and release pending exclusive ops.  */
void coroutine_fn bdrv_graph_co_rdunlock(void)
{
    AioContext *aiocontext;
    aiocontext = qemu_get_current_aio_context();

    qatomic_store_release(&aiocontext->reader_count,
                          aiocontext->reader_count - 1);
    /* make sure writer sees reader_count */
    smp_mb();

    /*
     * has_writer == 0: this means reader will read reader_count decreased
     * has_writer == 1: we don't know if writer read reader_count old or
     *                  new. Therefore, kick again so on next iteration
     *                  writer will for sure read the updated value.
     */
    if (qatomic_read(&has_writer)) {
        aio_wait_kick();
    }
}
