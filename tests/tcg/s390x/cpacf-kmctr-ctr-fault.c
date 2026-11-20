/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test for register update correctness in cpacf_aes_ctr (KMCTR AES-128 CTR).
 *
 * All the CPACF instructions are designed in a way that on exit
 * the registers are updated so that on incomplete processing (partial
 * completion) the instruction can just be re-invoked. The QEMU emulation
 * of the CPACF instructions needs to obey that rule as well.
 * This testcase checks the correct update of the register values by
 * triggering a page protection fault in the middle of the data. The
 * signal handler then checks for the expected register values. After
 * that the page is enabled for reading, causing the OS to restart the
 * instruction which continues to process the remaining data. Finally
 * the encryption result is checked to match the expectations.
 *
 * Test sequence:
 *  1. Allocate two contiguous, page-aligned source pages filled with
 *     pseudo-random data, two matching destination pages, and two
 *     counter pages filled with sequential counter blocks.
 *  2. Reference run: encrypt both source pages with cpacf_kmctr AES-128 CTR
 *     into dst_ref (no faults).
 *  3. Fault run:
 *     a. Protect the second source page (PROT_NONE).
 *     b. Install a SIGSEGV handler.
 *     c. Call cpacf_kmctr over both pages in one shot (2 * PAGE_SIZE bytes).
 *     d. On the SIGSEGV verify:
 *          - R4 (src address) equals exactly the start of the second source
 *            page (all blocks of page 1 were already consumed).
 *          - R5 (remaining src length) equals exactly PAGE_SIZE.
 *          - R2 (dst address) equals exactly dst_fault + PAGE_SIZE.
 *          - R6 (counter address) equals exactly ctr_pages + PAGE_SIZE,
 *            proving that cpacf_aes_ctr advanced the counter pointer
 *            block-by-block through page 1.
 *     e. Restore PROT_READ on the second source page and return from the
 *        handler so KMCTR re-executes from the updated registers,
 *        finishing the second page.
 *  4. Compare the fault-run output to the reference output; they must match.
 *
 * cpacf_km() in cpacf.h uses pinned registers R2/R4/R5 for its operands,
 * so the signal handler can read them reliably via ucontext_t.
 *
 * Usage: cpacf-kmctr-ctr-fault [-v]
 *   -v  verbose: print details about each step and every check.
 */

#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include "cpacf.h"

#define AES_BLOCK_SIZE  16

/* AES-128 key used for all encryptions in this test */
static const uint8_t aes128key[16] = {
    0xcc, 0x22, 0xda, 0x78, 0x7f, 0x37, 0x57, 0x11,
    0xc7, 0x63, 0x02, 0xbe, 0xf0, 0x97, 0x9d, 0x8e
};

/* Page size, determined at runtime */
static long page_size;

/* Verbose flag: set by -v on the command line */
static int verbose;

/*
 * State shared between main() and the SIGSEGV handler.
 * volatile prevents the compiler from optimising away accesses.
 */
static volatile int           sigsegv_count;  /* how many times handler fired */
static volatile int           sigsegv_pass;   /* 1 = all checks passed */
static volatile unsigned long expected_src;   /* exact expected R4 value */
static volatile unsigned long expected_dst;   /* exact expected R2 value */
static volatile unsigned long expected_ctr;   /* exact expected R6 value */

/*
 * SIGSEGV handler.
 *
 * At fault time the KMCTR instruction has already processed all blocks of the
 * first source page and advanced all four operand registers accordingly.
 * For the second-page fault:
 *   R2 = dst_fault + PAGE_SIZE   (destination pointer into second output page)
 *   R4 = src_pages + PAGE_SIZE   (source ptr at start of second source page)
 *   R5 = PAGE_SIZE               (exactly one page of source data remaining)
 *   R6 = ctr_pages + PAGE_SIZE   (counter ptr at start of second counter page)
 *
 * After verifying these values the handler restores read permission on the
 * second source page and returns. The signal return mechanism re-executes
 * the KMCTR instruction from the updated register state, completing the run.
 */
static void sigsegv_handler(int sig, siginfo_t *si, void *uctx_arg)
{
    /*
     * No printf within signal handler, but write() can be used. So this
     * little sig_printf() macro is a replacement to fprintf(stderr, ...)
     */
#define sig_printf(...) do { \
        char printfbuf[256]; \
        int len = snprintf(printfbuf, sizeof(printfbuf), __VA_ARGS__); \
        write(STDERR_FILENO, printfbuf, len); \
    } while (0)

    ucontext_t *uc = (ucontext_t *)uctx_arg;
    unsigned long r2, r4, r5, r6;
    int pass = 1;

    sigsegv_count++;

    /*
     * On s390x, glibc's mcontext_t exposes the GPRs as gregs[].
     */
    r2 = uc->uc_mcontext.gregs[2];  /* KMCTR destination address register */
    r4 = uc->uc_mcontext.gregs[4];  /* KMCTR source address register      */
    r5 = uc->uc_mcontext.gregs[5];  /* KMCTR source length register       */
    r6 = uc->uc_mcontext.gregs[6];  /* KMCTR counter address register     */

    if (verbose) {
        sig_printf("  SIGSEGV #%d: si_addr=0x%lx\n",
                   sigsegv_count, (unsigned long)si->si_addr);
        sig_printf("    R2 (dst addr):  0x%lx  (expected 0x%lx)\n",
                   r2, expected_dst);
        sig_printf("    R4 (src addr):  0x%lx  (expected 0x%lx)\n",
                   r4, expected_src);
        sig_printf("    R5 (src len):   0x%lx  (expected 0x%lx = PAGE_SIZE)\n",
                   r5, (unsigned long)page_size);
        sig_printf("    R6 (ctr addr):  0x%lx  (expected 0x%lx)\n",
                   r6, expected_ctr);
    }

    /* R4 must point exactly to the start of the second source page */
    if (r4 != expected_src) {
        sig_printf("FAIL: R4 (src addr) = 0x%lx, expected 0x%lx\n",
                   r4, expected_src);
        pass = 0;
    } else if (verbose) {
        sig_printf("    R4 check: PASS\n");
    }

    /* R5 must equal exactly PAGE_SIZE (one full page remaining) */
    if (r5 != (unsigned long)page_size) {
        sig_printf("FAIL: R5 (src len) = 0x%lx, expected PAGE_SIZE = 0x%lx\n",
                   r5, (unsigned long)page_size);
        pass = 0;
    } else if (verbose) {
        sig_printf("    R5 check: PASS\n");
    }

    /* R2 must point exactly to dst_fault + PAGE_SIZE */
    if (r2 != expected_dst) {
        sig_printf("FAIL: R2 (dst addr) = 0x%lx, expected 0x%lx\n",
                   r2, expected_dst);
        pass = 0;
    } else if (verbose) {
        sig_printf("    R2 check: PASS\n");
    }

    /* R6 must point exactly to ctr_pages + PAGE_SIZE */
    if (r6 != expected_ctr) {
        sig_printf("FAIL: R6 (ctr addr) = 0x%lx, expected 0x%lx\n",
                   r6, expected_ctr);
        pass = 0;
    } else if (verbose) {
        sig_printf("    R6 check: PASS\n");
    }

    sigsegv_pass = pass;

    if (verbose) {
        sig_printf("  restoring PROT_READ on page at 0x%lx, resuming KMCTR\n",
                   expected_src);
    }

    /* Restore read access so the restarted KMCTR can read the second page */
    mprotect((void *)expected_src, page_size, PROT_READ);

#undef sig_printf
}

int main(int argc, char *argv[])
{
    struct sigaction sa, old_sa;
    uint8_t *src_pages, *dst_ref, *dst_fault, *ctr_pages;
    /* KMCTR AES-128 parameter block: key only (16 bytes) */
    uint8_t param[16];
    unsigned long cc;
    int rc = 0;

    if (argc == 2 && strcmp(argv[1], "-v") == 0) {
        verbose = 1;
    } else if (argc > 1) {
        fprintf(stderr, "usage: %s [-v]\n", argv[0]);
        return EXIT_FAILURE;
    }

    page_size = getpagesize();

    if (verbose) {
        printf("page_size = %ld (0x%lx)\n", page_size, page_size);
    }

    /*
     * Allocate two contiguous page-aligned source pages and fill them with
     * a deterministic pseudo-random pattern so the test is reproducible.
     */
    src_pages = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (src_pages == MAP_FAILED) {
        perror("mmap src_pages");
        return EXIT_FAILURE;
    }
    for (long i = 0; i < 2 * page_size; i++) {
        src_pages[i] = (uint8_t)(i * 37 + 13);
    }

    /*
     * Allocate two contiguous counter pages. Each AES_BLOCK_SIZE-aligned
     * slot holds one counter block. Fill with a deterministic pattern that
     * serves as distinct counter values for each block position.
     */
    ctr_pages = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ctr_pages == MAP_FAILED) {
        perror("mmap ctr_pages");
        munmap(src_pages, 2 * page_size);
        return EXIT_FAILURE;
    }
    for (long i = 0; i < 2 * page_size; i++) {
        ctr_pages[i] = (uint8_t)(i * 7 + 5);
    }

    /* Reference destination: two contiguous pages, initially zeroed */
    dst_ref = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (dst_ref == MAP_FAILED) {
        perror("mmap dst_ref");
        munmap(src_pages, 2 * page_size);
        munmap(ctr_pages, 2 * page_size);
        return EXIT_FAILURE;
    }

    /* Fault-run destination: two contiguous pages, initially zeroed */
    dst_fault = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (dst_fault == MAP_FAILED) {
        perror("mmap dst_fault");
        munmap(src_pages, 2 * page_size);
        munmap(ctr_pages, 2 * page_size);
        munmap(dst_ref,   2 * page_size);
        return EXIT_FAILURE;
    }

    if (verbose) {
        printf("src_pages: [0x%lx, 0x%lx)  page1=[0x%lx, 0x%lx)  page2=[0x%lx, 0x%lx)\n",
               (unsigned long)src_pages,
               (unsigned long)(src_pages + 2 * page_size),
               (unsigned long)src_pages,
               (unsigned long)(src_pages + page_size),
               (unsigned long)(src_pages + page_size),
               (unsigned long)(src_pages + 2 * page_size));
        printf("ctr_pages: [0x%lx, 0x%lx)\n",
               (unsigned long)ctr_pages,
               (unsigned long)(ctr_pages + 2 * page_size));
        printf("dst_ref:   [0x%lx, 0x%lx)\n",
               (unsigned long)dst_ref,
               (unsigned long)(dst_ref + 2 * page_size));
        printf("dst_fault: [0x%lx, 0x%lx)\n",
               (unsigned long)dst_fault,
               (unsigned long)(dst_fault + 2 * page_size));
    }

    /* Build parameter block: key only for KMCTR AES-128 */
    memcpy(param, aes128key, sizeof(aes128key));

    /* ------------------------------------------------------------------ */
    /* Reference run: encrypt both pages with no fault injection.         */
    /* ------------------------------------------------------------------ */

    if (verbose) {
        printf("reference run: cpacf_kmctr AES-128 CTR over 2 pages (no faults)\n");
    }

    cpacf_kmctr(CPACF_KMCTR_AES_128, param, dst_ref,
                src_pages, 2 * page_size, ctr_pages, &cc);
    if (cc != 0) {
        fprintf(stderr, "FAIL: reference run unexpected cc=%lu\n", cc);
        rc = 1;
        goto out;
    }

    if (verbose) {
        printf("reference run: done, cc=0\n");
    }

    /* ------------------------------------------------------------------ */
    /* Fault run: protect second source page, run KMCTR over both pages.  */
    /* ------------------------------------------------------------------ */

    /* Record the exact register values we expect at fault time */
    expected_src  = (unsigned long)(src_pages + page_size);
    expected_dst  = (unsigned long)(dst_fault + page_size);
    expected_ctr  = (unsigned long)(ctr_pages + page_size);
    sigsegv_count = 0;
    sigsegv_pass  = 1;   /* handler sets to 0 on any check failure */

    if (verbose) {
        printf("fault run: expecting SIGSEGV when KMCTR reaches src page2 at 0x%lx\n",
               expected_src);
        printf("fault run: expected register state at fault:\n");
        printf("  R2 (dst addr) = 0x%lx\n", expected_dst);
        printf("  R4 (src addr) = 0x%lx\n", expected_src);
        printf("  R5 (src len)  = 0x%lx\n", (unsigned long)page_size);
        printf("  R6 (ctr addr) = 0x%lx\n", expected_ctr);
    }

    /* Install SIGSEGV handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags     = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, &old_sa) != 0) {
        perror("sigaction");
        rc = 1;
        goto out;
    }

    /* Protect the second source page to provoke a fault during KMCTR */
    if (mprotect(src_pages + page_size, page_size, PROT_NONE) != 0) {
        perror("mprotect PROT_NONE");
        sigaction(SIGSEGV, &old_sa, NULL);
        rc = 1;
        goto out;
    }

    if (verbose) {
        printf("fault run: second src page protected, starting cpacf_kmctr\n");
    }

    /*
     * Encrypt both pages with a single cpacf_kmctr call.
     *
     * Page 1 is processed normally; registers are advanced block-by-block
     * inside cpacf_aes_ctr. On the first read of the second source page the
     * CPU takes a protection fault. The signal handler verifies R2/R4/R5/R6
     * and restores PROT_READ. On return from the handler the KMCTR instruction
     * is restarted with the already-advanced registers so it processes only
     * the second page and completes with CC=0.
     *
     * The brc 1,0b loop inside cpacf_kmctr handles the unrelated CC=3
     * (partial-completion) case and is not triggered by the page fault.
     */
    memcpy(param, aes128key, sizeof(aes128key));
    cpacf_kmctr(CPACF_KMCTR_AES_128, param, dst_fault,
                src_pages, 2 * page_size, ctr_pages, &cc);

    /* Restore the original SIGSEGV disposition */
    sigaction(SIGSEGV, &old_sa, NULL);

    if (verbose) {
        printf("fault run: cpacf_kmctr returned, cc=%lu, SIGSEGV count=%d\n",
               cc, sigsegv_count);
    }

    if (cc != 0) {
        fprintf(stderr, "FAIL: fault run unexpected cc=%lu\n", cc);
        rc = 1;
    }

    /* ------------------------------------------------------------------ */
    /* Verify: signal handler must have fired exactly once.               */
    /* ------------------------------------------------------------------ */
    if (sigsegv_count != 1) {
        fprintf(stderr, "FAIL: expected 1 SIGSEGV, got %d\n", sigsegv_count);
        rc = 1;
    } else if (verbose) {
        printf("check: SIGSEGV count = %d (expected 1): PASS\n", sigsegv_count);
    }

    /* ------------------------------------------------------------------ */
    /* Verify: register checks inside the handler were correct.           */
    /* ------------------------------------------------------------------ */
    if (!sigsegv_pass) {
        fprintf(stderr, "FAIL: register check in SIGSEGV handler failed\n");
        rc = 1;
    } else if (verbose) {
        printf("check: register values in SIGSEGV handler: PASS\n");
    }

    /* ------------------------------------------------------------------ */
    /* Verify: fault-run ciphertext matches the reference ciphertext.     */
    /* ------------------------------------------------------------------ */
    if (memcmp(dst_ref, dst_fault, 2 * page_size) != 0) {
        fprintf(stderr, "FAIL: fault-run ciphertext does not match reference\n");
        rc = 1;
    } else if (verbose) {
        printf("check: fault-run ciphertext matches reference ciphertext: PASS\n");
    }

    if (rc == 0) {
        printf("cpacf-kmctr-ctr-fault: all checks passed\n");
    }

out:
    munmap(src_pages, 2 * page_size);
    munmap(ctr_pages, 2 * page_size);
    munmap(dst_ref,   2 * page_size);
    munmap(dst_fault, 2 * page_size);

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
