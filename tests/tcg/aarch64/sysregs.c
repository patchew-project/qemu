#include <stdio.h>

#define get_cpu_reg(id) ({                                      \
                unsigned long __val;                            \
                asm("mrs %0, "#id : "=r" (__val));              \
                printf("%-20s: 0x%016lx\n", #id, __val);        \
        })

int main(void)
{
    get_cpu_reg(cntvct_el0);
    get_cpu_reg(cntfrq_el0);
    return 0;
}
