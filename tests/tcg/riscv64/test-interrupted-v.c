/*
 * Test for interrupted vector operations.
 *
 * Some vector instructions can be interrupted partially complete, vstart will
 * be set to where the operation has progressed to, and the instruction can be
 * re-executed with vstart != 0. It is implementation dependent as to what
 * instructions can be interrupted and what vstart values are permitted when
 * executing them. Vector memory operations can typically be interrupted
 * (as they can take page faults), so these are easy to test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <riscv_vector.h>

static unsigned long page_size;

static volatile int nr_segv;
static volatile unsigned long fault_start, fault_end;

/*
 * Careful: qemu-user does not save/restore vector state in
 * signals yet, so any library or compiler autovec code will
 * corrupt our test.
 *
 * Do only minimal work in the signal handler.
 */
static void SEGV_handler(int signo, siginfo_t *info, void *context)
{
    unsigned long page = (unsigned long)info->si_addr &
                             ~(unsigned long)(page_size - 1);

    assert((unsigned long)info->si_addr >= fault_start);
    assert((unsigned long)info->si_addr < fault_end);
    mprotect((void *)page, page_size, PROT_READ);
    nr_segv++;
}

/* Use noinline to make generated code easier to inspect */
static __attribute__((noinline))
uint8_t unit_load(uint8_t *mem, size_t nr, bool ff)
{
    size_t vl;
    vuint8m1_t vec, redvec, sum;

    vl = __riscv_vsetvl_e8m1(nr);
    if (ff) {
        vec = __riscv_vle8ff_v_u8m1(mem, &vl, vl);
    } else {
        vec = __riscv_vle8_v_u8m1(mem, vl);
    }
    redvec = __riscv_vmv_v_x_u8m1(0, vl);
    sum = __riscv_vredsum_vs_u8m1_u8m1(vec, redvec, vl);
    return __riscv_vmv_x_s_u8m1_u8(sum);
}

static __attribute__((noinline))
uint8_t seg2_load(uint8_t *mem, size_t nr, bool ff)
{
    size_t vl;
    vuint8m1x2_t segvec;
    vuint8m1_t vec, redvec, sum;

    vl = __riscv_vsetvl_e8m1(nr);
    if (ff) {
        segvec = __riscv_vlseg2e8ff_v_u8m1x2(mem, &vl, vl);
    } else {
        segvec = __riscv_vlseg2e8_v_u8m1x2(mem, vl);
    }
    vec = __riscv_vadd_vv_u8m1(__riscv_vget_v_u8m1x2_u8m1(segvec, 0),
                   __riscv_vget_v_u8m1x2_u8m1(segvec, 1), vl);
    redvec = __riscv_vmv_v_x_u8m1(0, vl);
    sum = __riscv_vredsum_vs_u8m1_u8m1(vec, redvec, vl);
    return __riscv_vmv_x_s_u8m1_u8(sum);
}

static __attribute__((noinline))
uint8_t strided_load(uint8_t *mem, size_t nr, size_t stride)
{
    size_t vl;
    vuint8m1_t vec, redvec, sum;

    vl = __riscv_vsetvl_e8m1(nr);
    vec = __riscv_vlse8_v_u8m1(mem, stride, vl);
    redvec = __riscv_vmv_v_x_u8m1(0, vl);
    sum = __riscv_vredsum_vs_u8m1_u8m1(vec, redvec, vl);
    return __riscv_vmv_x_s_u8m1_u8(sum);
}

static __attribute__((noinline))
uint8_t indexed_load(uint8_t *mem, size_t nr, uint32_t *indices)
{
    size_t vl;
    vuint32m4_t idx;
    vuint8m1_t vec, redvec, sum;

    vl = __riscv_vsetvl_e8m1(nr);
    idx = __riscv_vle32_v_u32m4(indices, vl);
    vec = __riscv_vloxei32_v_u8m1(mem, idx, vl);
    redvec = __riscv_vmv_v_x_u8m1(0, vl);
    sum = __riscv_vredsum_vs_u8m1_u8m1(vec, redvec, vl);
    return __riscv_vmv_x_s_u8m1_u8(sum);
}

/* Use e8 elements, 128-bit vectors */
#define NR_ELEMS 16

static int run_interrupted_v_tests(void)
{
    struct sigaction act = { 0 };
    uint8_t *mem;
    uint32_t indices[NR_ELEMS];
    int i;

    page_size = sysconf(_SC_PAGESIZE);

    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = &SEGV_handler;
    if (sigaction(SIGSEGV, &act, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    mem = mmap(NULL, NR_ELEMS * page_size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mem != MAP_FAILED);
    madvise(mem, NR_ELEMS * page_size, MADV_NOHUGEPAGE);

    /* Unit-stride tests load memory crossing a page boundary */
    memset(mem, 0, NR_ELEMS * page_size);
    for (i = 0; i < NR_ELEMS; i++) {
        mem[page_size - NR_ELEMS + i] = 3;
    }
    for (i = 0; i < NR_ELEMS; i++) {
        mem[page_size + i] = 5;
    }

    nr_segv = 0;
    fault_start = (unsigned long)&mem[page_size - (NR_ELEMS / 2)];
    fault_end = fault_start + NR_ELEMS;
    mprotect(mem, page_size * 2, PROT_NONE);
    assert(unit_load(&mem[page_size - (NR_ELEMS / 2)], NR_ELEMS, false)
                    == 8 * NR_ELEMS / 2);
    assert(nr_segv == 2);

    nr_segv = 0;
    fault_start = (unsigned long)&mem[page_size - NR_ELEMS];
    fault_end = fault_start + NR_ELEMS * 2;
    mprotect(mem, page_size * 2, PROT_NONE);
    assert(seg2_load(&mem[page_size - NR_ELEMS], NR_ELEMS, false)
                    == 8 * NR_ELEMS);
    assert(nr_segv == 2);

    nr_segv = 0;
    fault_start = (unsigned long)&mem[page_size - (NR_ELEMS / 2)];
    fault_end = fault_start + (NR_ELEMS / 2);
    mprotect(mem, page_size * 2, PROT_NONE);
    assert(unit_load(&mem[page_size - (NR_ELEMS / 2)], NR_ELEMS, true)
                    == 3 * NR_ELEMS / 2);
    assert(nr_segv == 1); /* fault-first does not fault the second page */

    nr_segv = 0;
    fault_start = (unsigned long)&mem[page_size - NR_ELEMS];
    fault_end = fault_start + NR_ELEMS;
    mprotect(mem, page_size * 2, PROT_NONE);
    assert(seg2_load(&mem[page_size - NR_ELEMS], NR_ELEMS * 2, true)
                    == 3 * NR_ELEMS);
    assert(nr_segv == 1); /* fault-first does not fault the second page */

    /* Following tests load one element from first byte of each page */
    mprotect(mem, page_size * 2, PROT_READ | PROT_WRITE);
    memset(mem, 0, NR_ELEMS * page_size);
    for (i = 0; i < NR_ELEMS; i++) {
        mem[i * page_size] = 3;
        indices[i] = i * page_size;
    }

    nr_segv = 0;
    fault_start = (unsigned long)mem;
    fault_end = fault_start + NR_ELEMS * page_size;
    mprotect(mem, NR_ELEMS * page_size, PROT_NONE);
    assert(strided_load(mem, NR_ELEMS, page_size) == 3 * NR_ELEMS);
    assert(nr_segv == NR_ELEMS);

    nr_segv = 0;
    fault_start = (unsigned long)mem;
    fault_end = fault_start + NR_ELEMS * page_size;
    mprotect(mem, NR_ELEMS * page_size, PROT_NONE);
    assert(indexed_load(mem, NR_ELEMS, indices) == 3 * NR_ELEMS);
    assert(nr_segv == NR_ELEMS);

    munmap(mem, NR_ELEMS * page_size);

    return 0;
}

int main(void)
{
    return run_interrupted_v_tests();
}
