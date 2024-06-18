/*
 * QTest RISC-V CSR driver
 *
 * Copyright (c) 2024 Syntacore
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "../libqtest.h"
#include "csr.h"

static uint64_t qcsr_call(QTestState *qts, const char *name, uint64_t cpu,
                           int csrno, uint64_t *val)
{
    uint64_t res = 0;

    res = qtest_csr_call(qts, name, cpu, csrno, val);

    return res;
}

int qcsr_get_csr(QTestState *qts, uint64_t cpu,
        int csrno, uint64_t *val)
{
    int res;

    res = qcsr_call(qts, "get_csr", cpu, csrno, val);

    return res;
}

int qcsr_set_csr(QTestState *qts, uint64_t cpu,
        int csrno, uint64_t *val)
{
    int res;

    res = qcsr_call(qts, "set_csr", cpu, csrno, val);

    return res;
}
