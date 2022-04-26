#ifndef BLOCK_LOCK_H
#define BLOCK_LOCK_H

#include "qemu/osdep.h"

/*
 * register_aiocontext:
 * Add AioContext @ctx to the list of AioContext.
 * This list is used to obtain the total number of readers
 * currently running the graph.
 */
void register_aiocontext(AioContext *ctx);

/*
 * unregister_aiocontext:
 * Removes AioContext @ctx to the list of AioContext.
 */
void unregister_aiocontext(AioContext *ctx);

/*
 * bdrv_graph_wrlock:
 * Modify the graph. Nobody else is allowed to access the graph.
 * Set global has_writer to 1, so that the next readers will wait
 * that writer is done in a coroutine queue.
 * Then keep track of the running readers by counting what is the total
 * amount of readers (sum of all aiocontext readers), and wait until
 * they all finish with AIO_WAIT_WHILE.
 */
void bdrv_graph_wrlock(void);

/*
 * bdrv_graph_wrunlock:
 * Write finished, reset global has_writer to 0 and restart
 * all readers that are waiting.
 */
void bdrv_graph_wrunlock(void);

/*
 * bdrv_graph_co_rdlock:
 * Read the bs graph. Increases the reader counter of the current aiocontext,
 * and if has_writer is set, it means that the writer is modifying
 * the graph, therefore wait in a coroutine queue.
 * The writer will then wake this coroutine once it is done.
 */
void coroutine_fn bdrv_graph_co_rdlock(void);

/*
 * bdrv_graph_rdunlock:
 * Read terminated, decrease the count of readers in the current aiocontext.
 * If the writer is waiting for reads to finish (has_writer == 1), signal
 * the writer that we are done via aio_wait_kick() to let it continue.
 */
void coroutine_fn bdrv_graph_co_rdunlock(void);

/*
 * assert_bdrv_graph_readable:
 * Make sure that the reader is either the main loop,
 * or there is at least a reader helding the rdlock.
 * In this way an incoming writer is aware of the read and waits.
 */
void assert_bdrv_graph_readable(void);

/*
 * assert_bdrv_graph_writable:
 * Make sure that the writer is the main loop and has set @has_writer,
 * so that incoming readers will pause.
 */
void assert_bdrv_graph_writable(void);

typedef struct GraphLockable { } GraphLockable;

#define GML_OBJ_() (&(GraphLockable) { })

static inline GraphLockable *graph_lockable_auto_lock(GraphLockable *x)
{
    bdrv_graph_co_rdlock();
    return x;
}

static inline void graph_lockable_auto_unlock(GraphLockable *x)
{
    bdrv_graph_co_rdunlock();
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GraphLockable, graph_lockable_auto_unlock)

#define WITH_GRAPH_RDLOCK_GUARD_(var)                                         \
    for (g_autoptr(GraphLockable) var = graph_lockable_auto_lock(GML_OBJ_()); \
         var;                                                                 \
         graph_lockable_auto_unlock(var), var = NULL)

#define WITH_GRAPH_RDLOCK_GUARD() \
    WITH_GRAPH_RDLOCK_GUARD_(glue(graph_lockable_auto, __COUNTER__))

#define GRAPH_RDLOCK_GUARD(x)                                       \
    g_autoptr(GraphLockable)                                         \
    glue(graph_lockable_auto, __COUNTER__) G_GNUC_UNUSED =          \
            graph_lockable_auto_lock(GML_OBJ_())

#endif /* BLOCK_LOCK_H */

