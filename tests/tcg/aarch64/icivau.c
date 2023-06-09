/*
 * Tests the IC IVAU-driven workaround for catching changes made to dual-mapped
 * code that would otherwise go unnoticed in user mode.
 *
 * Copyright (c) 2023 Ericsson AB
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_CODE_SIZE 128

typedef int (SelfModTest)(uint32_t, uint32_t*);
typedef int (BasicTest)(int);

static void mark_code_modified(const uint32_t *exec_data, size_t length)
{
    size_t dcache_stride, icache_stride, i;
    unsigned long ctr_el0;

    /*
     * Step according to minimum cache sizes, as the cache maintenance
     * instructions operate on the cache line of the given address.
     *
     * We assume that exec_data is properly aligned.
     */
    asm ("mrs %0, ctr_el0\n" : "=r"(ctr_el0));
    dcache_stride = (4 << ((ctr_el0 >> 16) & 0xF));
    icache_stride = (4 << (ctr_el0 & 0xF));

    /*
     * For completeness we might be tempted to assert that we should fail when
     * the whole code update sequence is omitted, but that would make the test
     * flaky as it can succeed by coincidence on actual hardware.
     */
    for (i = 0; i < length; i += dcache_stride) {
        const char *dc_addr = &((const char *)exec_data)[i];
        asm volatile ("dc cvau, %x[dc_addr]\n"
                      : /* no outputs */
                      : [dc_addr] "r"(dc_addr)
                      : "memory");
    }

    asm volatile ("dmb ish\n");

    for (i = 0; i < length; i += icache_stride) {
        const char *ic_addr = &((const char *)exec_data)[i];
        asm volatile ("ic ivau, %x[ic_addr]\n"
                      : /* no outputs */
                      : [ic_addr] "r"(ic_addr)
                      : "memory");
    }

    asm volatile ("dmb ish\n"
                  "isb sy\n");
}

static int basic_test(uint32_t *rw_data, const uint32_t *exec_data)
{
    /*
     * As user mode only misbehaved for dual-mapped code when previously
     * translated code had been changed, we'll start off with this basic test
     * function to ensure that there's already some translated code at
     * exec_data before the next test. This should cause the next test to fail
     * if `mark_code_modified` fails to invalidate the code.
     *
     * Note that the payload is in binary form instead of inline assembler
     * because we cannot use __attribute__((naked)) on this platform and the
     * workarounds are at least as ugly as this is.
     */
    static const uint32_t basic_payload[] = {
        0xD65F03C0 /* 0x00: RET */
    };

    BasicTest *copied_ptr = (BasicTest *)exec_data;

    memcpy(rw_data, basic_payload, sizeof(basic_payload));
    mark_code_modified(exec_data, sizeof(basic_payload));

    return copied_ptr(1234) == 1234;
}

static int self_modification_test(uint32_t *rw_data, const uint32_t *exec_data)
{
    /*
     * This test is self-modifying in an attempt to cover an edge case where
     * the IC IVAU instruction invalidates itself.
     *
     * Note that the IC IVAU instruction is 16 bytes into the function, in what
     * will be the same cache line as the modifed instruction on machines with
     * a cache line size >= 16 bytes.
     */
    static const uint32_t self_mod_payload[] = {
        /* Overwrite the placeholder instruction with the new one. */
        0xB9001C20, /* 0x00: STR w0, [x1, 0x1C] */

        /* Get the executable address of the modified instruction. */
        0x100000A8, /* 0x04: ADR x8, <0x1C> */

        /* Mark the modified instruction as updated. */
        0xD50B7B28, /* 0x08: DC CVAU x8 */
        0xD5033BBF, /* 0x0C: DMB ISH */
        0xD50B7528, /* 0x10: IC IVAU x8 */
        0xD5033BBF, /* 0x14: DMB ISH */
        0xD5033FDF, /* 0x18: ISB */

        /* Placeholder instruction, overwritten above. */
        0x52800000, /* 0x1C: MOV w0, 0 */

        0xD65F03C0  /* 0x20: RET */
    };

    SelfModTest *copied_ptr = (SelfModTest *)exec_data;
    int i;

    memcpy(rw_data, self_mod_payload, sizeof(self_mod_payload));
    mark_code_modified(exec_data, sizeof(self_mod_payload));

    for (i = 1; i < 10; i++) {
        /* Replace the placeholder instruction with `MOV w0, i` */
        uint32_t new_instr = 0x52800000 | (i << 5);

        if (copied_ptr(new_instr, rw_data) != i) {
            return 0;
        }
    }

    return 1;
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

    if (ftruncate(fd, MAX_CODE_SIZE) == 0) {
        const uint32_t *exec_data;
        uint32_t *rw_data;

        rw_data = mmap(0, MAX_CODE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
        exec_data = mmap(0, MAX_CODE_SIZE, PROT_READ | PROT_EXEC,
                         MAP_SHARED, fd, 0);

        if (rw_data && exec_data) {
            if (basic_test(rw_data, exec_data) &&
                self_modification_test(rw_data, exec_data)) {
                return EXIT_SUCCESS;
            }
        }
    }

    return EXIT_FAILURE;
}
