/*
 * Check emulated system register access for linux-user mode.
 *
 * See: https://www.kernel.org/doc/Documentation/arm64/cpu-feature-registers.txt
 */

#include <asm/hwcap.h>
#include <stdio.h>
#include <sys/auxv.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>

#define get_cpu_reg(id) ({                                      \
            unsigned long __val = 0xdeadbeef;                   \
            asm("mrs %0, "#id : "=r" (__val));                  \
            printf("%-20s: 0x%016lx\n", #id, __val);            \
        })

bool should_fail;

int should_fail_count;
int should_not_fail_count;
uintptr_t failed_pc[10];

void sigill_handler(int signo, siginfo_t *si, void *data)
{
    ucontext_t *uc = (ucontext_t *)data;

    if (should_fail) {
        should_fail_count++;
    } else {
        uintptr_t pc = (uintptr_t) uc->uc_mcontext.pc;
        failed_pc[should_not_fail_count++] =  pc;
    }
    uc->uc_mcontext.pc += 4;
}

int main(void)
{
    struct sigaction sa;

    /* Hook in a SIGILL handler */
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = &sigill_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGILL, &sa, 0) != 0) {
        perror("sigaction");
        return 1;
    }

    /* since 4.12 */
    printf("Checking CNT registers\n");

    get_cpu_reg(ctr_el0);
    get_cpu_reg(cntvct_el0);
    get_cpu_reg(cntfrq_el0);

    /* when (getauxval(AT_HWCAP) & HWCAP_CPUID), since 4.11*/
    if (!(getauxval(AT_HWCAP) & HWCAP_CPUID)) {
        printf("CPUID registers unavailable\n");
        return 1;
    } else {
        printf("Checking CPUID registers\n");
    }

    get_cpu_reg(id_aa64isar0_el1);
    get_cpu_reg(id_aa64isar1_el1);
    get_cpu_reg(id_aa64mmfr0_el1);
    get_cpu_reg(id_aa64mmfr1_el1);
    get_cpu_reg(id_aa64pfr0_el1);
    get_cpu_reg(id_aa64pfr1_el1);
    get_cpu_reg(id_aa64dfr0_el1);
    get_cpu_reg(id_aa64dfr1_el1);

    get_cpu_reg(midr_el1);
    get_cpu_reg(mpidr_el1);
    get_cpu_reg(revidr_el1);

    printf("Remaining registers should fail\n");
    should_fail = true;

    /* Unexposed register access causes SIGILL */
    get_cpu_reg(id_mmfr0_el1);

    if (should_not_fail_count > 0) {
        int i;
        for (i = 0; i < should_not_fail_count; i++) {
            uintptr_t pc = failed_pc[i];
            uint32_t insn = *(uint32_t *) pc;
            printf("insn %#x @ %#lx unexpected FAIL\n", insn, pc);
        }
        return 1;
    }

    return should_fail_count == 1 ? 0 : 1;
}
