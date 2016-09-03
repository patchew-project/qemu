#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/host-utils.h"
#include "qemu/processor.h"

struct thread_info {
    uint64_t r;
} QEMU_ALIGNED(64);

struct count {
    unsigned long val;
} QEMU_ALIGNED(64);

static QemuThread *threads;
static struct thread_info *th_info;
static unsigned int n_threads = 1;
static unsigned int n_ready_threads;
static struct count *counts;
static unsigned long n_ops = 10000;
static double duration;
static unsigned int range = 1;
static bool test_start;

static const char commands_string[] =
    " -n = number of threads\n"
    " -o = number of ops per thread\n"
    " -r = range (will be rounded up to pow2)";

static void usage_complete(char *argv[])
{
    fprintf(stderr, "Usage: %s [options]\n", argv[0]);
    fprintf(stderr, "options:\n%s\n", commands_string);
}

/*
 * From: https://en.wikipedia.org/wiki/Xorshift
 * This is faster than rand_r(), and gives us a wider range (RAND_MAX is only
 * guaranteed to be >= INT_MAX).
 */
static uint64_t xorshift64star(uint64_t x)
{
    x ^= x >> 12; /* a */
    x ^= x << 25; /* b */
    x ^= x >> 27; /* c */
    return x * UINT64_C(2685821657736338717);
}

static void *thread_func(void *arg)
{
    struct thread_info *info = arg;
    unsigned long i;

    atomic_inc(&n_ready_threads);
    while (!atomic_mb_read(&test_start)) {
        cpu_relax();
    }

    for (i = 0; i < n_ops; i++) {
        unsigned int index;

        info->r = xorshift64star(info->r);
        index = info->r & (range - 1);
        atomic_inc(&counts[index].val);
    }
    return NULL;
}

static inline
uint64_t ts_subtract(const struct timespec *a, const struct timespec *b)
{
    uint64_t ns;

    ns = (b->tv_sec - a->tv_sec) * 1000000000ULL;
    ns += (b->tv_nsec - a->tv_nsec);
    return ns;
}

static void run_test(void)
{
    unsigned int i;
    struct timespec ts_start, ts_end;

    while (atomic_read(&n_ready_threads) != n_threads) {
        cpu_relax();
    }
    atomic_mb_set(&test_start, true);

    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    for (i = 0; i < n_threads; i++) {
        qemu_thread_join(&threads[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    duration = ts_subtract(&ts_start, &ts_end) / 1e9;
}

static void create_threads(void)
{
    unsigned int i;

    threads = g_new(QemuThread, n_threads);
    th_info = g_new(struct thread_info, n_threads);
    counts = qemu_memalign(64, sizeof(*counts) * range);

    for (i = 0; i < n_threads; i++) {
        struct thread_info *info = &th_info[i];

        info->r = (i + 1) ^ time(NULL);
        qemu_thread_create(&threads[i], NULL, thread_func, info,
                           QEMU_THREAD_JOINABLE);
    }
}

static void pr_params(void)
{
    printf("Parameters:\n");
    printf(" # of threads:      %u\n", n_threads);
    printf(" n_ops:             %lu\n", n_ops);
    printf(" ops' range:        %u\n", range);
}

static void pr_stats(void)
{
    unsigned long long val = 0;
    unsigned int i;
    double tx;

    for (i = 0; i < range; i++) {
        val += counts[i].val;
    }
    assert(val == n_threads * n_ops);
    tx = val / duration / 1e6;

    printf("Results:\n");
    printf("Duration:            %.2f s\n", duration);
    printf(" Throughput:         %.2f Mops/s\n", tx);
    printf(" Throughput/thread:  %.2f Mops/s/thread\n", tx / n_threads);
}

static void parse_args(int argc, char *argv[])
{
    unsigned long long n_ops_ull;
    int c;

    for (;;) {
        c = getopt(argc, argv, "hn:o:r:");
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'h':
            usage_complete(argv);
            exit(0);
        case 'n':
            n_threads = atoi(optarg);
            break;
        case 'o':
            n_ops_ull = atoll(optarg);
            if (n_ops_ull > ULONG_MAX) {
                fprintf(stderr,
                        "fatal: -o cannot be greater than %lu\n", ULONG_MAX);
                exit(1);
            }
            n_ops = n_ops_ull;
            break;
        case 'r':
            range = pow2ceil(atoi(optarg));
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    pr_params();
    create_threads();
    run_test();
    pr_stats();
    return 0;
}
