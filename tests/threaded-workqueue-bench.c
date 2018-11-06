/*
 * Threaded Workqueue Benchmark
 *
 * Author:
 *   Xiao Guangrong <xiaoguangrong@tencent.com>
 *
 * Copyright(C) 2018 Tencent Corporation.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include <zlib.h>

#include "qemu/osdep.h"
#include "exec/cpu-common.h"
#include "qemu/error-report.h"
#include "migration/qemu-file.h"
#include "qemu/threaded-workqueue.h"

#define PAGE_SHIFT              12
#define PAGE_SIZE               (1 << PAGE_SHIFT)
#define DEFAULT_THREAD_NR       2
#define DEFAULT_MEM_SIZE        1
#define DEFAULT_REPEATED_COUNT  3

static ssize_t test_writev_buffer(void *opaque, struct iovec *iov, int iovcnt,
                                   int64_t pos)
{
    int i, size = 0;

    for (i = 0; i < iovcnt; i++) {
        size += iov[i].iov_len;
    }
    return size;
}

static int test_fclose(void *opaque)
{
    return 0;
}

static const QEMUFileOps test_write_ops = {
    .writev_buffer  = test_writev_buffer,
    .close          = test_fclose
};

static QEMUFile *dest_file;

static const QEMUFileOps empty_ops = { };

struct CompressData {
    uint8_t *ram_addr;
    QEMUFile *file;
    z_stream stream;
};
typedef struct CompressData CompressData;

static int compress_request_size(void)
{
    return sizeof(CompressData);
}

static int compress_request_init(void *request)
{
    CompressData *cd = request;

    if (deflateInit(&cd->stream, 1) != Z_OK) {
        return -1;
    }
    cd->file = qemu_fopen_ops(NULL, &empty_ops);
    return 0;
}

static void compress_request_uninit(void *request)
{
    CompressData *cd = request;

    qemu_fclose(cd->file);
    deflateEnd(&cd->stream);
}

static void compress_thread_data_handler(void *request)
{
    CompressData *cd = request;
    int blen;

    blen = qemu_put_compression_data(cd->file, &cd->stream, cd->ram_addr,
                                     PAGE_SIZE);
    if (blen < 0) {
        error_report("compressed data failed!");
        qemu_file_set_error(dest_file, blen);
    }
}

struct CompressStats {
    unsigned long pages;
    unsigned long compressed_size;
};
typedef struct CompressStats CompressStats;

static CompressStats comp_stats;

static void compress_thread_data_done(void *request)
{
    CompressData *cd = request;
    int bytes_xmit;

    bytes_xmit = qemu_put_qemu_file(dest_file, cd->file);

    comp_stats.pages++;
    comp_stats.compressed_size += bytes_xmit;
}

static ThreadedWorkqueueOps ops = {
    .thread_get_request_size = compress_request_size,
    .thread_request_init = compress_request_init,
    .thread_request_uninit = compress_request_uninit,
    .thread_request_handler = compress_thread_data_handler,
    .thread_request_done = compress_thread_data_done,
};

static void compress_threads_save_cleanup(Threads *threads)
{
    threaded_workqueue_destroy(threads);
    qemu_fclose(dest_file);
}

static Threads *compress_threads_save_setup(int threads_nr, int requests_nr)
{
    Threads *compress_threads;

    dest_file = qemu_fopen_ops(NULL, &test_write_ops);
    compress_threads = threaded_workqueue_create("compress", threads_nr,
                                                 requests_nr, &ops);
    assert(compress_threads);
    return compress_threads;
}

static void compress_page_with_multi_thread(Threads *threads, uint8_t *addr)
{
    CompressData *cd;

retry:
    cd = threaded_workqueue_get_request(threads);
    if (!cd) {
        goto retry;
    }

    cd->ram_addr = addr;
    threaded_workqueue_submit_request(threads, cd);
}

static void run(Threads *threads, uint8_t *mem, unsigned long mem_size,
                int repeated_count)
{
    uint8_t *ptr = mem, *end = mem + mem_size;
    uint64_t start_ts, spend, total_ts = 0, pages = mem_size >> PAGE_SHIFT;
    double rate;
    int i;

    for (i = 0; i < repeated_count; i++) {
        ptr = mem;
        memset(&comp_stats, 0, sizeof(comp_stats));

        start_ts = g_get_monotonic_time();
        for (ptr = mem; ptr < end; ptr += PAGE_SIZE) {
            *ptr = 0x10;
            compress_page_with_multi_thread(threads, ptr);
        }
        threaded_workqueue_wait_for_requests(threads);
        spend = g_get_monotonic_time() - start_ts;
        total_ts += spend;

        if (comp_stats.pages != pages) {
            printf("ERROR: pages are compressed %ld, expect %ld.\n",
                   comp_stats.pages, pages);
            exit(-1);
        }

        rate = (double)(comp_stats.pages * PAGE_SIZE) /
                        comp_stats.compressed_size;
        printf("RUN %d: Request # %ld Cost %ld, Compression Rate %f.\n", i,
               comp_stats.pages, spend, rate);
    }

    printf("AVG: Time Cost %ld.\n", total_ts / repeated_count);
}

static void usage(const char *arg0)
{
    printf("\nThreaded Workqueue Benchmark.\n");
    printf("Usage:\n");
    printf("  %s [OPTIONS]\n", arg0);
    printf("Options:\n");
    printf("   -t        the number of threads (default %d).\n",
            DEFAULT_THREAD_NR);
    printf("   -r:       the number of requests handled by each thread (default %d).\n",
            DEFAULT_THREAD_REQUEST_NR);
    printf("   -m:       the size of the memory (G) used to test (default %dG).\n",
            DEFAULT_MEM_SIZE);
    printf("   -c:       the repeated count (default %d).\n",
            DEFAULT_REPEATED_COUNT);
    printf("   -h        show this help info.\n");
}

int main(int argc, char *argv[])
{
    int c, threads_nr, requests_nr, repeated_count;
    unsigned long mem_size;
    uint8_t *mem;
    Threads *threads;

    threads_nr = DEFAULT_THREAD_NR;
    requests_nr = DEFAULT_THREAD_REQUEST_NR;
    mem_size = DEFAULT_MEM_SIZE;
    repeated_count = DEFAULT_REPEATED_COUNT;

    for (;;) {
        c = getopt(argc, argv, "t:r:m:c:h");
        if (c < 0) {
            break;
        }

        switch (c) {
        case 't':
            threads_nr = atoi(optarg);
            break;
        case 'r':
            requests_nr = atoi(optarg);
            break;
        case 'm':
            mem_size = atol(optarg);
            break;
        case 'c':
            repeated_count = atoi(optarg);
            break;
        default:
            printf("Unkown option: %c.\n", c);
        case 'h':
            usage(argv[0]);
            return -1;
        }
    }

    printf("Run the benchmark: threads %d requests-per-thread: %d memory %ldG repeat %d.\n",
            threads_nr, requests_nr, mem_size, repeated_count);

    mem_size = mem_size << 30;
    mem = qemu_memalign(PAGE_SIZE, mem_size);
    memset(mem, 0, mem_size);

    threads = compress_threads_save_setup(threads_nr, requests_nr);
    run(threads, mem, mem_size, repeated_count);
    compress_threads_save_cleanup(threads);
    return 0;
}
