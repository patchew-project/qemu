/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test for register and IV update correctness in cpacf_aes_cbc
 * (KMC AES-128 CBC).
 *
 * All the CPACF instructions are designed in a way that on exit
 * the registers and associated memory (like an IV) are updated so
 * that on incomplete processing (partial completion) the instruction
 * can just be re-invoked. The QEMU emulation of the CPACF instructions
 * needs to obey that rule as well.
 * This testcase checks the correct update of the register values and
 * IV in memory by triggering a page protection fault in the middle of
 * the data. The signal handler then checks for the expected register
 * and memory values. After that the page is enabled for reading,
 * causing the OS to restart the instruction which continues to process
 * the remaining data. Finally the encryption result is checked to
 * match the expectations.
 *
 * Test sequence:
 *  1. Allocate two contiguous, page-aligned source pages filled with
 *     pseudo-random data and two matching destination pages.
 *  2. Reference run - two steps to capture the intermediate IV:
 *     a. Encrypt page 1 only with cpacf_kmc AES-128 CBC into dst_ref.
 *        After this call the IV in the parameter block has been updated
 *        to the running IV after the last block of page 1. Save this IV
 *        as expected_iv[].
 *     b. Encrypt page 2 with the same parameter block (IV already advanced)
 *        into dst_ref + PAGE_SIZE.
 *  3. Fault run:
 *     a. Protect the second source page (PROT_NONE).
 *     b. Install a SIGSEGV handler.
 *     c. Call cpacf_kmc over both pages in one shot (2 * PAGE_SIZE bytes).
 *     d. On the SIGSEGV verify:
 *          - R4 (src address) equals exactly the start of the second source
 *            page (all blocks of page 1 were already consumed).
 *          - R5 (remaining src length) equals exactly PAGE_SIZE.
 *          - R2 (dst address) equals exactly dst_fault + PAGE_SIZE.
 *          - The IV at param_fault[0..15] equals expected_iv[], proving
 *            that cpacf_aes_cbc wrote the correct running IV back into the
 *            parameter block before the fault.
 *     e. Restore PROT_READ on the second source page and return from the
 *        handler so KMC re-executes from the updated registers and IV,
 *        finishing the second page.
 *  4. Compare the fault-run output to the reference output; they must match.
 *
 * cpacf_kmc() in cpacf.h uses pinned registers R2/R4/R5 for its operands,
 * so the signal handler can read them reliably via ucontext_t.
 *
 * Usage: cpacf-kmc-cbc-fault [-v]
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

/* AES-128 key and initial IV used for all encryptions in this test */
static const uint8_t aes128key[16] = {
    0x1f, 0x8e, 0x49, 0x73, 0x95, 0x3f, 0x3f, 0xb0,
    0xbd, 0x6b, 0x16, 0x66, 0x2e, 0x9a, 0x3c, 0x17
};
static const uint8_t initial_iv[16] = {
    0x2f, 0xe2, 0xb3, 0x33, 0xce, 0xda, 0x8f, 0x98,
    0xf4, 0xa9, 0x9b, 0x40, 0xd2, 0xcd, 0x34, 0xa8
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
static volatile uint8_t      *fault_param;   /* param block used in fault run */
static uint8_t                expected_iv[AES_BLOCK_SIZE]; /* IV after page 1 */

/*
 * SIGSEGV handler.
 *
 * At fault time the KMC instruction has already processed all blocks of the
 * first source page and updated registers and parameter block accordingly.
 * For the second-page fault:
 *   R2 = dst_fault + PAGE_SIZE  (destination pointer into second output page)
 *   R4 = src_pages + PAGE_SIZE  (source pointer at start of second source page)
 *   R5 = PAGE_SIZE              (exactly one page of source data remaining)
 *   fault_param[0..15]          (IV in param block = running IV after page 1)
 *
 * After verifying these values the handler restores read permission on the
 * second source page and returns. The signal return mechanism re-executes
 * the KMC instruction from the updated register state and IV, completing
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
    r2 = uc->uc_mcontext.gregs[2];  /* KMC destination address register */
    r4 = uc->uc_mcontext.gregs[4];  /* KMC source address register      */
    r5 = uc->uc_mcontext.gregs[5];  /* KMC source length register       */

    if (verbose) {
        sig_printf("  SIGSEGV #%d: si_addr=0x%lx\n",
                   sigsegv_count, (unsigned long)si->si_addr);
        sig_printf("    R2 (dst addr):  0x%lx  (expected 0x%lx)\n",
                   r2, expected_dst);
        sig_printf("    R4 (src addr):  0x%lx  (expected 0x%lx)\n",
                   r4, expected_src);
        sig_printf("    R5 (src len):   0x%lx  (expected 0x%lx = PAGE_SIZE)\n",
                   r5, (unsigned long)page_size);
        sig_printf("    IV in param block: ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            sig_printf("%02x", ((uint8_t *)fault_param)[i]);
        }
        sig_printf("\n");
        sig_printf("    expected IV:       ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            sig_printf("%02x", expected_iv[i]);
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
     * The IV at param_fault[0..15] must equal expected_iv[], i.e. the
     * running IV that cpacf_aes_cbc wrote back after consuming page 1.
     */
    if (memcmp((void *)fault_param, expected_iv, AES_BLOCK_SIZE) != 0) {
        sig_printf("FAIL: IV in param block at fault does not match expected IV\n");
        sig_printf("  got:      ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            sig_printf("%02x", ((uint8_t *)fault_param)[i]);
        }
        sig_printf("\n  expected: ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            sig_printf("%02x", expected_iv[i]);
        }
        sig_printf("\n");
        pass = 0;
    } else if (verbose) {
        sig_printf("    IV check: PASS\n");
    }

    sigsegv_pass = pass;

    if (verbose) {
        sig_printf("  restoring PROT_READ on page at 0x%lx, resuming KMC\n",
                   expected_src);
    }

    /* Restore read access so the restarted KMC can read the second page */
    mprotect((void *)expected_src, page_size, PROT_READ);

#undef sig_printf
}

/*
 * Build a KMC AES-128 parameter block at buf[]:
 *   buf[0..15]  = iv
 *   buf[16..31] = key
 */
static void build_param(uint8_t *buf, const uint8_t *iv, const uint8_t *key)
{
    memcpy(buf,      iv,  AES_BLOCK_SIZE);
    memcpy(buf + AES_BLOCK_SIZE, key, 16);
}

int main(int argc, char *argv[])
{
    struct sigaction sa, old_sa;
    uint8_t *src_pages, *dst_ref, *dst_fault;
    /* parameter blocks: [IV(16)][key(16)] = 32 bytes */
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
    /* After this call param_ref[0..15] contains the running IV after the  */
    /* last block of page 1. Save it as expected_iv[] so the signal handler*/
    /* can verify the fault-run param block contains the same value.       */
    /* ------------------------------------------------------------------ */

    build_param(param_ref, initial_iv, aes128key);

    if (verbose) {
        printf("reference run step 1: cpacf_kmc AES-128 CBC, page 1 only\n");
    }

    cpacf_kmc(CPACF_KMC_AES_128, param_ref, dst_ref, src_pages, page_size, &cc);
    if (cc != 0) {
        fprintf(stderr, "FAIL: reference run step 1 unexpected cc=%lu\n", cc);
        rc = 1;
        goto out;
    }

    /* Capture the running IV after page 1 - this is in param_ref[0..15] */
    memcpy(expected_iv, param_ref, AES_BLOCK_SIZE);

    if (verbose) {
        printf("reference run step 1: done, cc=0\n");
        printf("  running IV after page 1 (= expected_iv): ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            printf("%02x", expected_iv[i]);
        }
        printf("\n");
    }

    /* ------------------------------------------------------------------ */
    /* Reference run - step 2: encrypt page 2 with the advanced IV.       */
    /* param_ref already holds the updated IV from step 1.                */
    /* ------------------------------------------------------------------ */

    if (verbose) {
        printf("reference run step 2: cpacf_kmc AES-128 CBC, page 2 only\n");
    }

    cpacf_kmc(CPACF_KMC_AES_128, param_ref, dst_ref + page_size,
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
    /* Fault run: protect second source page, run KMC over both pages.    */
    /* ------------------------------------------------------------------ */

    build_param(param_fault, initial_iv, aes128key);

    /* Record the exact values we expect at fault time */
    expected_src  = (unsigned long)(src_pages + page_size);
    expected_dst  = (unsigned long)(dst_fault + page_size);
    fault_param   = param_fault;
    sigsegv_count = 0;
    sigsegv_pass  = 1;   /* handler sets to 0 on any check failure */

    if (verbose) {
        printf("fault run: expecting SIGSEGV when KMC reaches src page2 at 0x%lx\n",
               expected_src);
        printf("fault run: expected register state at fault:\n");
        printf("  R2 (dst addr) = 0x%lx\n", expected_dst);
        printf("  R4 (src addr) = 0x%lx\n", expected_src);
        printf("  R5 (src len)  = 0x%lx\n", (unsigned long)page_size);
        printf("fault run: expected IV in param block at fault: ");
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            printf("%02x", expected_iv[i]);
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

    /* Protect the second source page to provoke a fault during KMC */
    if (mprotect(src_pages + page_size, page_size, PROT_NONE) != 0) {
        perror("mprotect PROT_NONE");
        sigaction(SIGSEGV, &old_sa, NULL);
        rc = 1;
        goto out;
    }

    if (verbose) {
        printf("fault run: second src page protected, starting cpacf_kmc\n");
    }

    /*
     * Encrypt both pages with a single cpacf_kmc call.
     *
     * Page 1 is processed normally; registers and the IV in param_fault
     * are advanced block-by-block inside cpacf_aes_cbc. On the first read
     * of the second source page the CPU takes a protection fault.
     * The signal handler verifies R2/R4/R5 and the IV, then restores
     * PROT_READ. On return from the handler the KMC instruction is
     * restarted with the already-advanced registers and correct IV, so it
     * processes only the second page and completes with CC=0.
     *
     * The brc 1,0b loop inside cpacf_kmc handles the unrelated CC=3
     * (partial-completion) case and is not triggered by the page fault.
     */
    cpacf_kmc(CPACF_KMC_AES_128, param_fault, dst_fault,
              src_pages, 2 * page_size, &cc);

    /* Restore the original SIGSEGV disposition */
    sigaction(SIGSEGV, &old_sa, NULL);

    if (verbose) {
        printf("fault run: cpacf_kmc returned, cc=%lu, SIGSEGV count=%d\n",
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
    /* Verify: register and IV checks inside the handler were correct.    */
    /* ------------------------------------------------------------------ */
    if (!sigsegv_pass) {
        fprintf(stderr, "FAIL: register/IV check in SIGSEGV handler failed\n");
        rc = 1;
    } else if (verbose) {
        printf("check: register and IV values in SIGSEGV handler: PASS\n");
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
        printf("cpacf-kmc-cbc-fault: all checks passed\n");
    }

out:
    munmap(src_pages, 2 * page_size);
    munmap(dst_ref,   2 * page_size);
    munmap(dst_fault, 2 * page_size);

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
