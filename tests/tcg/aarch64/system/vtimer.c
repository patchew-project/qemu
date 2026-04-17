/*
 * Simple Virtual Timer Test
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <minilib.h>
#include "sysregs.h"

int main(void)
{
    int i;

    ml_printf("VTimer Test\n");

    write_sysreg(1, cntvoff_el2);
    write_sysreg(-1, cntv_cval_el0);
    write_sysreg(1, cntv_ctl_el0);

    ml_printf("cntvoff_el2=%lx\n", read_sysreg(cntvoff_el2));
    ml_printf("cntv_cval_el0=%lx\n", read_sysreg(cntv_cval_el0));
    ml_printf("cntv_ctl_el0=%lx\n", read_sysreg(cntv_ctl_el0));

    /* Now read cval a few times */
    for (i = 0; i < 10; i++) {
        ml_printf("%d: cntv_cval_el0=%lx\n", i, read_sysreg(cntv_cval_el0));
    }

    return 0;
}
