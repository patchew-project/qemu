/*
 * ARM Barrier Litmus Tests
 *
 * This test provides a framework for testing barrier conditions on
 * the processor. It's simpler than the more involved barrier testing
 * frameworks as we are looking for simple failures of QEMU's TCG not
 * weird edge cases the silicon gets wrong.
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

/*
 * Memory barriers from atomic.h
 *
 * While it would be nice to include atomic.h directly that
 * complicates the build. However we can assume a modern compilers
 * with the full set of __atomic C11 primitives.
 */

#define barrier()          ({ asm volatile("" ::: "memory"); (void)0; })
#define smp_mb()           ({ barrier(); __atomic_thread_fence(__ATOMIC_SEQ_CST); })
#define smp_mb_release()   ({ barrier(); __atomic_thread_fence(__ATOMIC_RELEASE); })
#define smp_mb_acquire()   ({ barrier(); __atomic_thread_fence(__ATOMIC_ACQUIRE); })

#define smp_wmb()          smp_mb_release()
#define smp_rmb()          smp_mb_acquire()

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define MAX_THREADS 2

/* Array size and access controls */
static int array_size = 100000;
static int wait_if_ahead = 0;

/*
 * These test_array_* structures are a contiguous array modified by two or more
 * competing CPUs. The padding is to ensure the variables do not share
 * cache lines.
 *
 * All structures start zeroed.
 */

typedef struct test_array
{
    volatile unsigned int x;
    uint8_t dummy[64];
    volatile unsigned int y;
    uint8_t dummy2[64];
    volatile unsigned int r[MAX_THREADS];
} test_array;

/* Test definition structure
 *
 * The first function will always run on the primary CPU, it is
 * usually the one that will detect any weirdness and trigger the
 * failure of the test.
 */

typedef int (*test_fn)(void *arg);
typedef void * (*thread_fn)(void *arg);

typedef struct {
    char *test_name;
    bool  should_pass;
    test_fn main_fn;
    thread_fn secondary_fn;
} test_descr_t;

/*
 * Synchronisation Helpers
 */

pthread_barrier_t sync_barrier;

static void init_sync_point(void)
{
    pthread_barrier_init(&sync_barrier, NULL, 2);
    smp_mb();
}

static inline void wait_for_main_thread()
{
    pthread_barrier_wait(&sync_barrier);
}

static inline void wake_up_secondary_thread()
{
    pthread_barrier_wait(&sync_barrier);
}

/*
 * Litmus tests
 */

/* Simple Message Passing
 *
 * x is the message data
 * y is the flag to indicate the data is ready
 *
 * Reading x == 0 when y == 1 is a failure.
 */

static void * message_passing_write(void *arg)
{
    int i;
    test_array *array = (test_array *) arg;

    wait_for_main_thread();

    for (i = 0; i < array_size; i++) {
        volatile test_array *entry = &array[i];
        entry->x = 1;
        entry->y = 1;
    }

    return NULL;
}

static int message_passing_read(void *arg)
{
    int i;
    int errors = 0, ready = 0;
    test_array *array = (test_array *) arg;

    wake_up_secondary_thread();

    for (i = 0; i < array_size; i++) {
        volatile test_array *entry = &array[i];
        unsigned int x,y;
        y = entry->y;
        x = entry->x;

        if (y && !x)
            errors++;
        ready += y;
    }

    return errors;
}

/* Simple Message Passing with barriers */
static void * message_passing_write_barrier(void *arg)
{
    int i;
    test_array *array = (test_array *) arg;

    wait_for_main_thread();

    for (i = 0; i< array_size; i++) {
        volatile test_array *entry = &array[i];
        entry->x = 1;
        smp_wmb();
        entry->y = 1;
    }

    return NULL;
}

static int message_passing_read_barrier(void *arg)
{
    int i;
    int errors = 0, ready = 0, not_ready = 0;
    test_array *array = (test_array *) arg;

    wake_up_secondary_thread();

    for (i = 0; i < array_size; i++) {
        volatile test_array *entry = &array[i];
        unsigned int x, y;
        y = entry->y;
        smp_rmb();
        x = entry->x;

        if (y && !x)
            errors++;

        if (y) {
            ready++;
        } else {
            not_ready++;

            if (not_ready > 2) {
                entry = &array[i+1];
                do {
                    not_ready = 0;
                } while (wait_if_ahead && !entry->y);
            }
        }
    }

    return errors;
}

/* Simple Message Passing with Acquire/Release */
static void * message_passing_write_release(void *arg)
{
    int i;
    test_array *array = (test_array *) arg;

    for (i=0; i< array_size; i++) {
        volatile test_array *entry = &array[i];
        entry->x = 1;
        __atomic_store_n(&entry->y, 1, __ATOMIC_RELEASE);
    }

    return NULL;
}

static int message_passing_read_acquire(void *arg)
{
    int i;
    int errors = 0, ready = 0, not_ready = 0;
    test_array *array = (test_array *) arg;

    for (i = 0; i < array_size; i++) {
        volatile test_array *entry = &array[i];
        unsigned int x, y;
        __atomic_load(&entry->y, &y, __ATOMIC_ACQUIRE);
        x = entry->x;

        if (y && !x)
            errors++;

        if (y) {
            ready++;
        } else {
            not_ready++;

            if (not_ready > 2) {
                entry = &array[i+1];
                do {
                    not_ready = 0;
                } while (wait_if_ahead && !entry->y);
            }
        }
    }

    return errors;
}

/*
 * Store after load
 *
 * T1: write 1 to x, load r from y
 * T2: write 1 to y, load r from x
 *
 * Without memory fence r[0] && r[1] == 0
 * With memory fence both == 0 should be impossible
 */

static int check_store_and_load_results(const char *name, int thread, test_array *array)
{
    int i;
    int neither = 0;
    int only_first = 0;
    int only_second = 0;
    int both = 0;

    for (i=0; i< array_size; i++) {
        volatile test_array *entry = &array[i];
        if (entry->r[0] == 0 &&
            entry->r[1] == 0) {
            neither++;
        } else if (entry->r[0] &&
            entry->r[1]) {
            both++;
        } else if (entry->r[0]) {
            only_first++;
        } else {
            only_second++;
        }
    }

    printf("T%d: neither=%d only_t1=%d only_t2=%d both=%d\n", thread,
           neither, only_first, only_second, both);

    return neither;
}

/*
 * This attempts to synchronise the start of both threads to roughly
 * the same time. On real hardware there is a little latency as the
 * secondary vCPUs are powered up however this effect it much more
 * exaggerated on a TCG host.
 *
 * Busy waits until the we pass a future point in time, returns final
 * start time.
 */

static int store_and_load_1(void *arg)
{
    int i;
    test_array *array = (test_array *) arg;

    wake_up_secondary_thread();

    for (i = 0; i < array_size; i++) {
        volatile test_array *entry = &array[i];
        unsigned int r;
        entry->x = 1;
        r = entry->y;
        entry->r[0] = r;
    }

    return check_store_and_load_results("sal", 1, array);
}

static void * store_and_load_2(void *arg)
{
    int i;
    test_array *array = (test_array *) arg;

    wait_for_main_thread();

    for (i = 0; i < array_size; i++) {
        volatile test_array *entry = &array[i];
        unsigned int r;
        entry->y = 1;
        r = entry->x;
        entry->r[1] = r;
    }

    check_store_and_load_results("sal", 2, array);

    return NULL;
}

static int store_and_load_barrier_1(void *arg)
{
    int i;
    test_array *array = (test_array *) arg;

    wake_up_secondary_thread();

    for (i = 0; i < array_size; i++) {
        volatile test_array *entry = &array[i];
        unsigned int r;
        entry->x = 1;
        smp_mb();
        r = entry->y;
        entry->r[0] = r;
    }

    smp_mb();

    return check_store_and_load_results("sal_barrier", 1, array);
}

static void * store_and_load_barrier_2(void *arg)
{
    int i;
    test_array *array = (test_array *) arg;

    wait_for_main_thread();

    for (i = 0; i < array_size; i++) {
        volatile test_array *entry = &array[i];
        unsigned int r;
        entry->y = 1;
        smp_mb();
        r = entry->x;
        entry->r[1] = r;
    }

    check_store_and_load_results("sal_barrier", 2, array);

    return NULL;
}


/* Test array */
static test_descr_t tests[] = {

    { "mp",         false,
      message_passing_read,
      message_passing_write
    },

    { "mp_barrier", true,
      message_passing_read_barrier,
      message_passing_write_barrier
    },

    { "mp_acqrel", true,
      message_passing_read_acquire,
      message_passing_write_release
    },

    { "sal",       false,
      store_and_load_1,
      store_and_load_2
    },

    { "sal_barrier", true,
      store_and_load_barrier_1,
      store_and_load_barrier_2
    },
};


int setup_and_run_litmus(test_descr_t *test)
{
    pthread_t tid1;
    int res;
    test_array *array;

    printf("Running test: %s\n", test->test_name);
    array = calloc(array_size, sizeof(test_array));
    printf("Allocated test array @ %p\n", array);

    init_sync_point();

    if (array) {
        pthread_create(&tid1, NULL, test->secondary_fn, array);
        res = test->main_fn(array);
    } else {
        printf("%s: failed to allocate memory", test->test_name);
        res = 1;
    }

    /* ensure secondary thread has finished */
    pthread_join(tid1, NULL);

    free(array);
    array = NULL;

    return res;
}

int main(int argc, char **argv)
{
    int i;
    int res = 0;

    for (i = 0; i < argc; i++) {
        char *arg = argv[i];
        unsigned int j;

        /* Test modifiers */
        if (strstr(arg, "count=") != NULL) {
            char *p = strstr(arg, "=");
            array_size = atol(p+1);
            continue;
        } else if (strcmp (arg, "wait") == 0) {
            wait_if_ahead = 1;
            continue;
        } else if (strcmp(arg, "help") == 0) {
            printf("Tests: ");
            for (j = 0; j < ARRAY_SIZE(tests); j++) {
                printf("%s ", tests[j].test_name);
            }
            printf("\n");
        }

        for (j = 0; j < ARRAY_SIZE(tests); j++) {
            if (strcmp(arg, tests[j].test_name) == 0) {
                res += setup_and_run_litmus(&tests[j]);
                continue;
            }
        }
    }

    return res;
}
