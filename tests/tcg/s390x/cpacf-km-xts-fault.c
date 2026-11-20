/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test for register and tweak update correctness in cpacf_aes_xts
 * (KM AES-128 XTS).
 *
 * All the CPACF instructions are designed in a way that on exit
 * the registers and associated memory (like a running XTS tweak) are
 * updated so that on incomplete processing (partial completion) the
 * instruction can just be re-invoked. The QEMU emulation of the CPACF
 * instructions needs to obey that rule as well.
 * This testcase checks the correct update of the register values and
 * tweak in memory by triggering a page protection fault in the middle
 * of the data. The signal handler then checks for the expected register
 * and memory values. After that the page is enabled for reading,
 * causing the OS to restart the instruction which continues to process
 * the remaining data. Finally the encryption result is checked to
 * match the expectations.
 *
 * Test sequence:
 *  1. Allocate two contiguous, page-aligned source pages filled with
 *     pseudo-random data and two matching destination pages.
 *  2. Compute the initial XTS tweak value by encrypting a sector number
 *     block with key2 using plain AES-128 ECB (as specified by XTS-AES).
 *  3. Reference run - two steps to capture the intermediate tweak:
 *     a. Encrypt page 1 only with cpacf_km KM_XTS_128 into dst_ref.
 *        After this call param_ref[16..31] contains the running tweak for
 *        the first block of page 2. Save this as expected_tweak[].
 *     b. Encrypt page 2 with the same parameter block (tweak already
 *        advanced) into dst_ref + PAGE_SIZE.
 *  4. Fault run:
 *     a. Protect the second source page (PROT_NONE).
 *     b. Install a SIGSEGV handler.
 *     c. Call cpacf_km with KM_XTS_128 over both pages in one shot.
 *     d. On the SIGSEGV verify:
 *          - R4 (src address) equals exactly the start of the second source
 *            page (all blocks of page 1 were already consumed).
 *          - R5 (remaining src length) equals exactly PAGE_SIZE.
 *          - R2 (dst address) equals exactly dst_fault + PAGE_SIZE.
 *          - param_fault[16..31] equals expected_tweak[], proving that
 *            cpacf_aes_xts wrote the correct advanced tweak back into the
 *            parameter block before the fault.
 *     e. Restore PROT_READ on the second source page and return from the
 *        handler so KM re-executes from the updated registers and tweak,
 *        finishing the second page.
 *  5. Compare the fault-run output to the reference output; they must match.
 *
 * cpacf_km() in cpacf.h uses pinned registers R2/R4/R5 for its operands,
 * so the signal handler can read them reliably via ucontext_t.
 *
 * Usage: cpacf-km-xts-fault [-v]
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

/*
 * XTS-128 uses two AES-128 keys.
 *   key1 is used to encrypt/decrypt the data blocks.
 *   key2 is used once to derive the initial tweak from the sector number.
 */
static const uint8_t xts128key1[16] = {
    0xa1, 0xb9, 0x0c, 0xba, 0x3f, 0x06, 0xac, 0x35,
    0x3b, 0x2c, 0x34, 0x38, 0x76, 0x08, 0x17, 0x62
};
static const uint8_t xts128key2[16] = {
    0x09, 0x09, 0x23, 0x02, 0x6e, 0x91, 0x77, 0x18,
    0x15, 0xf2, 0x9d, 0xab, 0x01, 0x93, 0x2f, 0x2f
};
/* Sector number block used to derive the initial tweak */
static const uint8_t xts128sect[16] = {
    0x4f, 0xae, 0xf7, 0x11, 0x7c, 0xda, 0x59, 0xc6,
    0x6e, 0x4b, 0x92, 0x01, 0x3e, 0x76, 0x8a, 0xd5
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
static volatile int           sigsegv_pass;  /* 1 = all checks passed */
static volatile unsigned long expected_src;  /* exact expected R4 value */
static volatile unsigned long expected_dst;  /* exact expected R2 value */
static volatile uint8_t *fault_param;        /* param block used in fault run */
static uint8_t           expected_tweak[AES_BLOCK_SIZE]; /* tweak after pg 1 */

/*
 * SIGSEGV handler.
 *
 * At fault time the KM instruction has already processed all blocks of the
 * first source page and updated registers and parameter block accordingly.
 * For the second-page fault:
 *   R2 = dst_fault + PAGE_SIZE   (destination pointer into second output page)
 *   R4 = src_pages + PAGE_SIZE   (src pointer at start of second source page)
 *   R5 = PAGE_SIZE               (exactly one page of source data remaining)
 *   fault_param[keysize..keysize+15] = tweak for first block of page 2
 *
 * After verifying these values the handler restores read permission on the
 * second source page and returns. The signal return mechanism re-executes
 * the KM instruction from the updated register state and tweak, completing
 * the second page.
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
        /* tweak is at param[keysize..keysize+15]; keysize=16 for XTS-128 */
        sig_printf("    tweak in param block: ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            sig_printf("%02x", ((uint8_t *)fault_param)[AES_BLOCK_SIZE + i]);
        }
        sig_printf("\n");
        sig_printf("    expected tweak:       ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            sig_printf("%02x", expected_tweak[i]);
        }
        sig_printf("\n");
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

    /*
     * The tweak at fault_param[AES_BLOCK_SIZE..2*AES_BLOCK_SIZE-1] must
     * equal expected_tweak[], i.e. the tweak that cpacf_aes_xts wrote back
     * after processing the last block of page 1 (= tweak for first block of
     * page 2).
     */
    if (memcmp((void *)(fault_param + AES_BLOCK_SIZE),
               expected_tweak, AES_BLOCK_SIZE) != 0) {
        sig_printf("FAIL: tweak in param block at fault does not match expected tweak\n");
        sig_printf("  got:      ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            sig_printf("%02x", ((uint8_t *)fault_param)[AES_BLOCK_SIZE + i]);
        }
        sig_printf("\n  expected: ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            sig_printf("%02x", expected_tweak[i]);
        }
        sig_printf("\n");
        pass = 0;
    } else if (verbose) {
        sig_printf("    tweak check: PASS\n");
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

/*
 * Build a KM XTS-128 parameter block at buf[]:
 *   buf[0..15]  = key1
 *   buf[16..31] = initial tweak (computed from sector number)
 *
 * The initial tweak is derived by encrypting the sector number block with
 * key2 using plain AES-128 ECB, as specified by the XTS-AES standard.
 */
static int build_xts128_param(uint8_t *buf)
{
    uint8_t key2_param[16];
    unsigned long cc = 0;

    memcpy(key2_param, xts128key2, sizeof(xts128key2));
    cpacf_km(CPACF_KM_AES_128, key2_param, buf + AES_BLOCK_SIZE,
             xts128sect, AES_BLOCK_SIZE, &cc);
    if (cc != 0) {
        return -1;
    }
    memcpy(buf, xts128key1, sizeof(xts128key1));
    return 0;
}

int main(int argc, char *argv[])
{
    struct sigaction sa, old_sa;
    uint8_t *src_pages, *dst_ref, *dst_fault;
    /* KM XTS-128 parameter block: [key1(16)][tweak(16)] = 32 bytes */
    uint8_t param_ref[32];
    uint8_t param_fault[32];
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
    /* Reference run - step 1: encrypt page 1 only.                       */
    /*                                                                     */
    /* After this call param_ref[16..31] contains the running tweak for   */
    /* the first block of page 2. Save it as expected_tweak[] so the      */
    /* signal handler can verify the fault-run param block matches.       */
    /* ------------------------------------------------------------------ */

    if (build_xts128_param(param_ref) != 0) {
        fprintf(stderr, "FAIL: initial tweak derivation failed\n");
        rc = 1;
        goto out;
    }

    if (verbose) {
        printf("reference run step 1: cpacf_km KM_XTS_128, page 1 only\n");
        printf("  initial tweak: ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            printf("%02x", param_ref[AES_BLOCK_SIZE + i]);
        }
        printf("\n");
    }

    cpacf_km(CPACF_KM_XTS_128, param_ref, dst_ref, src_pages, page_size, &cc);
    if (cc != 0) {
        fprintf(stderr, "FAIL: reference run step 1 unexpected cc=%lu\n", cc);
        rc = 1;
        goto out;
    }

    /* Capture the running tweak after page 1 - this is in param_ref[16..31] */
    memcpy(expected_tweak, param_ref + AES_BLOCK_SIZE, AES_BLOCK_SIZE);

    if (verbose) {
        printf("reference run step 1: done, cc=0\n");
        printf("  running tweak after page 1 (= expected_tweak): ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            printf("%02x", expected_tweak[i]);
        }
        printf("\n");
    }

    /* ------------------------------------------------------------------ */
    /* Reference run - step 2: encrypt page 2 with the advanced tweak.   */
    /* param_ref already holds the updated tweak from step 1.             */
    /* ------------------------------------------------------------------ */

    if (verbose) {
        printf("reference run step 2: cpacf_km KM_XTS_128, page 2 only\n");
    }

    cpacf_km(CPACF_KM_XTS_128, param_ref, dst_ref + page_size,
             src_pages + page_size, page_size, &cc);
    if (cc != 0) {
        fprintf(stderr, "FAIL: reference run step 2 unexpected cc=%lu\n", cc);
        rc = 1;
        goto out;
    }

    if (verbose) {
        printf("reference run step 2: done, cc=0\n");
    }

    /* ------------------------------------------------------------------ */
    /* Fault run: protect second source page, run KM over both pages.     */
    /* ------------------------------------------------------------------ */

    if (build_xts128_param(param_fault) != 0) {
        fprintf(stderr, "FAIL: fault param derivation failed\n");
        rc = 1;
        goto out;
    }

    /* Record the exact values we expect at fault time */
    expected_src  = (unsigned long)(src_pages + page_size);
    expected_dst  = (unsigned long)(dst_fault + page_size);
    fault_param   = param_fault;
    sigsegv_count = 0;
    sigsegv_pass  = 1;   /* handler sets to 0 on any check failure */

    if (verbose) {
        printf("fault run: expecting SIGSEGV when KM reaches src page2 at 0x%lx\n",
               expected_src);
        printf("fault run: expected register state at fault:\n");
        printf("  R2 (dst addr) = 0x%lx\n", expected_dst);
        printf("  R4 (src addr) = 0x%lx\n", expected_src);
        printf("  R5 (src len)  = 0x%lx\n", (unsigned long)page_size);
        printf("fault run: expected tweak in param block at fault: ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            printf("%02x", expected_tweak[i]);
        }
        printf("\n");
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
     * Encrypt both pages with a single cpacf_km XTS call.
     *
     * Page 1 is processed normally; registers and the tweak in param_fault
     * are advanced block-by-block inside cpacf_aes_xts. On the first read
     * of the second source page the CPU takes a protection fault.
     * The signal handler verifies R2/R4/R5 and the tweak, then restores
     * PROT_READ. On return from the handler the KM instruction is restarted
     * with the already-advanced registers and correct tweak, so it processes
     * only the second page and completes with CC=0.
     *
     * The brc 1,0b loop inside cpacf_km handles the unrelated CC=3
     * (partial-completion) case and is not triggered by the page fault.
     */
    cpacf_km(CPACF_KM_XTS_128, param_fault, dst_fault,
             src_pages, 2 * page_size, &cc);

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
    /* Verify: register and tweak checks inside the handler were correct. */
    /* ------------------------------------------------------------------ */
    if (!sigsegv_pass) {
        fprintf(stderr, "FAIL: register/tweak check in SIGSEGV handler failed\n");
        rc = 1;
    } else if (verbose) {
        printf("check: register and tweak values in SIGSEGV handler: PASS\n");
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
        printf("cpacf-km-xts-fault: all checks passed\n");
    }

out:
    munmap(src_pages, 2 * page_size);
    munmap(dst_ref,   2 * page_size);
    munmap(dst_fault, 2 * page_size);

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
