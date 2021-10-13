/*
 * Copyright (c) 2021, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PPC_MCE_H
#define HW_PPC_MCE_H

typedef struct PPCMceInjectionParams {
    uint64_t srr1_mask;
    uint32_t dsisr;
    uint64_t dar;
    bool recovered;
} PPCMceInjectionParams;

typedef struct PPCMceInjection PPCMceInjection;

#define TYPE_PPC_MCE_INJECTION "ppc-mce-injection"
#define PPC_MCE_INJECTION(obj) \
    INTERFACE_CHECK(PPCMceInjection, (obj), TYPE_PPC_MCE_INJECTION)
typedef struct PPCMceInjectionClass PPCMceInjectionClass;
DECLARE_CLASS_CHECKERS(PPCMceInjectionClass, PPC_MCE_INJECTION,
                       TYPE_PPC_MCE_INJECTION)

struct PPCMceInjectionClass {
    InterfaceClass parent_class;
    void (*inject_mce)(CPUState *cs, PPCMceInjectionParams *p);
};

#endif
