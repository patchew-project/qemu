/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test for register update correctness in cpacf_aes_ecb (KM AES-128 ECB).
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
 *     pseudo-random data and two matching destination pages.
 *  2. Reference run: encrypt both source pages with cpacf_km AES-128 ECB
 *     into the reference destination buffer (no faults).
 *  3. Fault run:
 *     a. Protect the second source page (PROT_NONE).
 *     b. Install a SIGSEGV handler.
 *     c. Call cpacf_km over both pages (2 * PAGE_SIZE bytes).
 *     d. On the SIGSEGV verify:
 *          - si_addr lies within the second source page.
 *          - R4 (src address) equals exactly the start of the second source
 *            page (all blocks of page 1 were already consumed).
 *          - R5 (remaining src length) equals exactly PAGE_SIZE.
 *          - R2 (dst address) equals exactly dst_fault + PAGE_SIZE.
 *     e. Restore PROT_READ on the second source page and return from the
 *        handler so KM re-executes from the updated registers and finishes.
 *  4. Compare the fault-run output to the reference output; they must match.
 *
 * cpacf_km() in cpacf.h uses pinned registers R2/R4/R5 for its operands,
 * so the signal handler can read them reliably via ucontext_t.
 *
 * Usage: cpacf-km-ecb-fault [-v]
 *   -v  verbose: print details about each step and every register check.
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

/* AES-128 key used for all encryptions in this test */
static const uint8_t aes128key[16] = {
    0xed, 0xfd, 0xb2, 0x57, 0xcb, 0x37, 0xcd, 0xf1,
    0x82, 0xc5, 0x45, 0x5b, 0x0c, 0x0e, 0xfe, 0xbb
};

/* Page size, determined at runtime */
static long page_size;

/* Verbose flag: set by -v on the command line */
static int verbose;

/*
 * State shared between main() and the SIGSEGV handler.
 * volatile prevents the compiler from optimising away accesses.
 */
static volatile int           sigsegv_count; /* how many times handler fired */
static volatile int           sigsegv_pass;  /* 1 = all register checks fine */
static volatile unsigned long expected_src;  /* exact expected R4 value */
static volatile unsigned long expected_dst;  /* exact expected R2 value */

/*
 * SIGSEGV handler.
 *
 * At fault time the KM instruction has already advanced its operand registers
 * past all fully processed blocks. For the second-page fault:
 *   R2 = dst_fault + PAGE_SIZE  (destination pointer into second output page)
 *   R4 = src_pages + PAGE_SIZE  (source pointer at start of second source page)
 *   R5 = PAGE_SIZE              (exactly one page of source data remaining)
 * Note that this requires using the inline function cpacf_km() from cpacf.h
 * which uses pinned registers for the parameters and this has to be in
 * alignment with the code here.
 *
 * After verifying these values the handler restores read permission on the
 * second source page and returns. The signal return mechanism re-executes
 * the KM instruction from the updated register state, completing the run.
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
    unsigned long r2, r4, r5;
    int pass = 1;

    sigsegv_count++;

    /*
     * On s390x, glibc's mcontext_t exposes the GPRs as gregs[].
     */
    r2 = uc->uc_mcontext.gregs[2];  /* KM destination address register */
    r4 = uc->uc_mcontext.gregs[4];  /* KM source address register      */
    r5 = uc->uc_mcontext.gregs[5];  /* KM source length register       */

    if (verbose) {
        sig_printf("  SIGSEGV #%d: si_addr=0x%lx\n",
                   sigsegv_count, (unsigned long)si->si_addr);
        sig_printf("    R2 (dst addr):  0x%lx  (expected 0x%lx)\n",
                   r2, expected_dst);
        sig_printf("    R4 (src addr):  0x%lx  (expected 0x%lx)\n",
                   r4, expected_src);
        sig_printf("    R5 (src len):   0x%lx  (expected 0x%lx = PAGE_SIZE)\n",
                   r5, (unsigned long)page_size);
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

    sigsegv_pass = pass;

    if (verbose) {
        sig_printf("  restoring PROT_READ on page at 0x%lx, resuming KM\n",
                   expected_src);
    }

    /* Restore read access so the restarted KM can read the second page */
    mprotect((void *)expected_src, page_size, PROT_READ);

#undef sig_printf
}

int main(int argc, char *argv[])
{
    struct sigaction sa, old_sa;
    uint8_t *src_pages, *dst_ref, *dst_fault;
    uint8_t param[16];       /* AES-128 parameter block (key only for ECB) */
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

    /* Reference destination: two contiguous pages, initially zeroed */
    dst_ref = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (dst_ref == MAP_FAILED) {
        perror("mmap dst_ref");
        munmap(src_pages, 2 * page_size);
        return EXIT_FAILURE;
    }

    /* Fault-run destination: two contiguous pages, initially zeroed */
    dst_fault = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (dst_fault == MAP_FAILED) {
        perror("mmap dst_fault");
        munmap(src_pages, 2 * page_size);
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
        printf("dst_ref:   [0x%lx, 0x%lx)\n",
               (unsigned long)dst_ref,
               (unsigned long)(dst_ref + 2 * page_size));
        printf("dst_fault: [0x%lx, 0x%lx)\n",
               (unsigned long)dst_fault,
               (unsigned long)(dst_fault + 2 * page_size));
    }

    /* ------------------------------------------------------------------ */
    /* Reference run: encrypt both pages with no fault injection.         */
    /* ------------------------------------------------------------------ */

    if (verbose) {
        printf("reference run: cpacf_km AES-128 ECB over 2 pages (no faults)\n");
    }

    memcpy(param, aes128key, sizeof(aes128key));
    cpacf_km(CPACF_KM_AES_128, param, dst_ref, src_pages, 2 * page_size, &cc);
    if (cc != 0) {
        fprintf(stderr, "FAIL: reference run unexpected cc=%lu\n", cc);
        rc = 1;
        goto out;
    }

    if (verbose) {
        printf("reference run: done, cc=0\n");
    }

    /* ------------------------------------------------------------------ */
    /* Fault run: protect second source page, run KM over both pages.     */
    /* ------------------------------------------------------------------ */

    /* Record the exact register values we expect at fault time */
    expected_src = (unsigned long)(src_pages + page_size);
    expected_dst = (unsigned long)(dst_fault + page_size);
    sigsegv_count = 0;
    sigsegv_pass  = 1;   /* handler sets to 0 on any check failure */

    if (verbose) {
        printf("fault run: expecting SIGSEGV when KM reaches src page2 at 0x%lx\n",
               expected_src);
        printf("fault run: expected register state at fault:\n");
        printf("  R2 (dst addr) = 0x%lx\n", expected_dst);
        printf("  R4 (src addr) = 0x%lx\n", expected_src);
        printf("  R5 (src len)  = 0x%lx\n", (unsigned long)page_size);
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

    /* Protect the second source page to provoke a fault during KM */
    if (mprotect(src_pages + page_size, page_size, PROT_NONE) != 0) {
        perror("mprotect PROT_NONE");
        sigaction(SIGSEGV, &old_sa, NULL);
        rc = 1;
        goto out;
    }

    if (verbose) {
        printf("fault run: second src page protected, starting cpacf_km\n");
    }

    /*
     * Encrypt both pages with a single cpacf_km call.
     *
     * The first PAGE_SIZE bytes (page 1) are processed normally; the
     * registers are advanced block-by-block inside cpacf_aes_ecb. On the
     * first read of the second source page the CPU takes a protection fault.
     * The signal handler verifies R2/R4/R5 and restores PROT_READ. On
     * return from the handler the KM instruction is restarted with the
     * already-advanced registers so it processes only the second page and
     * completes with CC=0.
     *
     * The brc 1,0b loop inside cpacf_km handles the unrelated CC=3
     * (partial-completion) case and is not triggered by the page fault.
     */
    memcpy(param, aes128key, sizeof(aes128key));
    cpacf_km(CPACF_KM_AES_128, param, dst_fault, src_pages, 2 * page_size, &cc);

    /* Restore the original SIGSEGV disposition */
    sigaction(SIGSEGV, &old_sa, NULL);

    if (verbose) {
        printf("fault run: cpacf_km returned, cc=%lu, SIGSEGV count=%d\n",
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
    /* Verify: register values inside the handler were correct.           */
    /* ------------------------------------------------------------------ */
    if (!sigsegv_pass) {
        fprintf(stderr, "FAIL: register value check in SIGSEGV handler failed\n");
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
        printf("cpacf-km-ecb-fault: all checks passed\n");
    }

out:
    munmap(src_pages, 2 * page_size);
    munmap(dst_ref,   2 * page_size);
    munmap(dst_fault, 2 * page_size);

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
