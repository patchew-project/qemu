#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define PAYLOAD_SIZE (256)

typedef int (*SelfModTestPtr)(char *, const char*, int);
typedef int (*CompareTestPtr)(int, int);

void flush_icache(const char *exec_data, size_t length)
{
    size_t dcache_stride, icache_stride, i;
    unsigned long ctr_el0;

    /*
     * Step according to minimum cache sizes, as the cache maintenance
     * instructions operate on the cache line of the given address.
     *
     * We assume that exec_data is properly aligned.
     */
    __asm__("mrs %0, ctr_el0\n" : "=r"(ctr_el0));
    dcache_stride = (4 << ((ctr_el0 >> 16) & 0xF));
    icache_stride = (4 << (ctr_el0 & 0xF));

    for (i = 0; i < length; i += dcache_stride) {
        const char *dc_addr = &exec_data[i];
        __asm__ ("dc cvau, %x[dc_addr]\n"
                 : /* no outputs */
                 : [dc_addr] "r"(dc_addr)
                 : "memory");
    }

    __asm__ ("dmb ish\n");

    for (i = 0; i < length; i += icache_stride) {
        const char *ic_addr = &exec_data[i];
        __asm__ ("ic ivau, %x[ic_addr]\n"
                 : /* no outputs */
                 : [ic_addr] "r"(ic_addr)
                 : "memory");
    }

    __asm__ ("dmb ish\n"
             "isb sy\n");
}

/*
 * The unmodified assembly of this function returns 0, it self-modifies to
 * return the value indicated by new_move.
 */
int self_modification_payload(char *rw_data, const char *exec_data,
                              int new_move)
{
    register int result __asm__ ("w0") = new_move;

    __asm__ (/* Get the writable address of __modify_me. */
             "sub %x[rw_data], %x[rw_data], %x[exec_data]\n"
             "adr %x[exec_data], __modify_me\n"
             "add %x[rw_data], %x[rw_data], %x[exec_data]\n"
             /* Overwrite the `MOV W0, #0` with the new move. */
             "str %w[result], [%x[rw_data]]\n"
             /*
              * Mark the code as modified.
              *
              * Note that we align to the nearest 64 bytes in an attempt to put
              * the flush sequence in the same cache line as the modified move.
              */
             ".align 6\n"
             "dc cvau, %x[exec_data]\n"
             ".align 2\n"
             "dmb ish\n"
             "ic ivau, %x[exec_data]\n"
             "dmb ish\n"
             "isb sy\n"
             "__modify_me: mov w0, #0x0\n"
             : [result] "+r"(result),
               [rw_data] "+r"(rw_data),
               [exec_data] "+r"(exec_data)
             : /* No untouched inputs */
             : "memory");

    return result;
}

int self_modification_test(char *rw_data, const char *exec_data)
{
    SelfModTestPtr copied_ptr = (SelfModTestPtr)exec_data;
    int i;

    /*
     * Bluntly assumes that the payload is position-independent and not larger
     * than PAYLOAD_SIZE.
     */
    memcpy(rw_data, self_modification_payload, PAYLOAD_SIZE);

    /*
     * Notify all PEs that the code at exec_data has been altered.
     *
     * For completeness we could assert that we should fail when this is
     * omitted, which works in user mode and on actual hardware as the
     * modification won't "take," but doesn't work in system mode as the
     * softmmu handles everything for us.
     */
    flush_icache(exec_data, PAYLOAD_SIZE);

    for (i = 1; i < 10; i++) {
        const int mov_w0_template = 0x52800000;

        /* MOV W0, i */
        if (copied_ptr(rw_data, exec_data, mov_w0_template | (i << 5)) != i) {
            return 0;
        }
    }

    return 1;
}

int compare_copied(char *rw_data, const char *exec_data,
                   int (*reference_ptr)(int, int))
{
    CompareTestPtr copied_ptr = (CompareTestPtr)exec_data;
    int a, b;

    memcpy(rw_data, reference_ptr, PAYLOAD_SIZE);
    flush_icache(exec_data, PAYLOAD_SIZE);

    for (a = 1; a < 10; a++) {
        for (b = 1; b < 10; b++) {
            if (copied_ptr(a, b) != reference_ptr(a, b)) {
                return 0;
            }
        }
    }

    return 1;
}

int compare_alpha(int a, int b)
{
    return a + b;
}

int compare_beta(int a, int b)
{
    return a - b;
}

int compare_gamma(int a, int b)
{
    return a * b;
}

int compare_delta(int a, int b)
{
    return a / b;
}

int main(int argc, char **argv)
{
    const char *shm_name = "qemu-test-tcg-aarch64-icivau";
    int fd;

    fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

    if (fd < 0) {
        return EXIT_FAILURE;
    }

    /* Unlink early to avoid leaving garbage in case the test crashes. */
    shm_unlink(shm_name);

    if (ftruncate(fd, PAYLOAD_SIZE) == 0) {
        const char *exec_data;
        char *rw_data;

        rw_data = mmap(0, PAYLOAD_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd, 0);
        exec_data = mmap(0, PAYLOAD_SIZE, PROT_READ | PROT_EXEC, MAP_SHARED,
                         fd, 0);

        if (rw_data && exec_data) {
            CompareTestPtr compare_tests[4] = {compare_alpha,
                                               compare_beta,
                                               compare_gamma,
                                               compare_delta};
            int success, i;

            success = self_modification_test(rw_data, exec_data);

            for (i = 0; i < 4; i++) {
                success &= compare_copied(rw_data, exec_data, compare_tests[i]);
            }

            if (success) {
                return EXIT_SUCCESS;
            }
        }
    }

    return EXIT_FAILURE;
}
