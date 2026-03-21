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
static volatile bool fault_write;

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
    if (fault_write) {
        mprotect((void *)page, page_size, PROT_READ | PROT_WRITE);
    } else {
        mprotect((void *)page, page_size, PROT_READ);
    }
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

/* New store functions */
static __attribute__((noinline))
void unit_store(uint8_t *mem, size_t nr, vuint8m1_t vec)
{
    size_t vl;

    vl = __riscv_vsetvl_e8m1(nr);
    __riscv_vse8_v_u8m1(mem, vec, vl);
}

static __attribute__((noinline))
void seg2_store(uint8_t *mem, size_t nr, vuint8m1x2_t segvec)
{
    size_t vl;

    vl = __riscv_vsetvl_e8m1(nr);
    __riscv_vsseg2e8_v_u8m1x2(mem, segvec, vl);
}

static __attribute__((noinline))
void strided_store(uint8_t *mem, size_t nr, size_t stride, vuint8m1_t vec)
{
    size_t vl;

    vl = __riscv_vsetvl_e8m1(nr);
    __riscv_vsse8_v_u8m1(mem, stride, vec, vl);
}

static __attribute__((noinline))
void indexed_store(uint8_t *mem, size_t nr, uint32_t *indices, vuint8m1_t vec)
{
    size_t vl;
    vuint32m4_t idx;

    vl = __riscv_vsetvl_e8m1(nr);
    idx = __riscv_vle32_v_u32m4(indices, vl);
    __riscv_vsoxei32_v_u8m1(mem, idx, vec, vl);
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

    /*** Load tests ***/
    fault_write = false;

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

    /*** Store tests ***/
    fault_write = true;

    uint8_t store_data[NR_ELEMS];
    uint8_t store_data_seg0[NR_ELEMS];
    uint8_t store_data_seg1[NR_ELEMS];
    vuint8m1_t vec;
    vuint8m1x2_t segvec;
    size_t vl = __riscv_vsetvl_e8m1(NR_ELEMS);

    /* Create some data to store */
    for (i = 0; i < NR_ELEMS; i++) {
        store_data[i] = i * 3;
        store_data_seg0[i] = i * 5;
        store_data_seg1[i] = i * 7;
    }
    vec = __riscv_vle8_v_u8m1(store_data, vl);
    segvec = __riscv_vcreate_v_u8m1x2(
        __riscv_vle8_v_u8m1(store_data_seg0, vl),
        __riscv_vle8_v_u8m1(store_data_seg1, vl));

    /* Unit-stride store test crossing a page boundary */
    mprotect(mem, page_size * 2, PROT_READ | PROT_WRITE);
    memset(mem, 0, page_size * 2);
    nr_segv = 0;
    fault_start = (unsigned long)&mem[page_size - (NR_ELEMS / 2)];
    fault_end = fault_start + NR_ELEMS;
    mprotect(mem, page_size * 2, PROT_NONE);
    unit_store(&mem[page_size - (NR_ELEMS / 2)], NR_ELEMS, vec);
    assert(nr_segv == 2);
    for (i = 0; i < NR_ELEMS; i++) {
        assert(mem[page_size - (NR_ELEMS / 2) + i] == store_data[i]);
    }

    /* Segmented store test crossing a page boundary */
    mprotect(mem, page_size * 2, PROT_READ | PROT_WRITE);
    memset(mem, 0, page_size * 2);
    nr_segv = 0;
    fault_start = (unsigned long)&mem[page_size - NR_ELEMS];
    fault_end = fault_start + NR_ELEMS * 2;
    mprotect(mem, page_size * 2, PROT_NONE);
    seg2_store(&mem[page_size - NR_ELEMS], NR_ELEMS, segvec);
    assert(nr_segv == 2);
    for (i = 0; i < NR_ELEMS; i++) {
        assert(mem[page_size - NR_ELEMS + i * 2] == store_data_seg0[i]);
        assert(mem[page_size - NR_ELEMS + i * 2 + 1] == store_data_seg1[i]);
    }

    /* Strided store test to one element on each page */
    mprotect(mem, NR_ELEMS * page_size, PROT_READ | PROT_WRITE);
    memset(mem, 0, NR_ELEMS * page_size);
    nr_segv = 0;
    fault_start = (unsigned long)mem;
    fault_end = fault_start + NR_ELEMS * page_size;
    mprotect(mem, NR_ELEMS * page_size, PROT_NONE);
    strided_store(mem, NR_ELEMS, page_size, vec);
    assert(nr_segv == NR_ELEMS);
    for (i = 0; i < NR_ELEMS; i++) {
        assert(mem[i * page_size] == store_data[i]);
    }

    /* Indexed store test to one element on each page */
    mprotect(mem, NR_ELEMS * page_size, PROT_READ | PROT_WRITE);
    memset(mem, 0, NR_ELEMS * page_size);
    nr_segv = 0;
    fault_start = (unsigned long)mem;
    fault_end = fault_start + NR_ELEMS * page_size;
    mprotect(mem, NR_ELEMS * page_size, PROT_NONE);
    indexed_store(mem, NR_ELEMS, indices, vec);
    assert(nr_segv == NR_ELEMS);
    for (i = 0; i < NR_ELEMS; i++) {
        assert(mem[indices[i]] == store_data[i]);
    }

    munmap(mem, NR_ELEMS * page_size);

    return 0;
}

int main(void)
{
    return run_interrupted_v_tests();
}
