/*
 * Copyright (C) 2022, Linaro Ltd.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/atomic128.h"

#ifdef __x86_64__
#include "qemu/cpuid.h"

#ifndef signature_INTEL_ecx
/* "Genu ineI ntel" */
#define signature_INTEL_ebx     0x756e6547
#define signature_INTEL_edx     0x49656e69
#define signature_INTEL_ecx     0x6c65746e
#endif

/*
 * The latest Intel SDM has added:
 *     Processors that enumerate support for Intel® AVX (by setting
 *     the feature flag CPUID.01H:ECX.AVX[bit 28]) guarantee that the
 *     16-byte memory operations performed by the following instructions
 *     will always be carried out atomically:
 *      - MOVAPD, MOVAPS, and MOVDQA.
 *      - VMOVAPD, VMOVAPS, and VMOVDQA when encoded with VEX.128.
 *      - VMOVAPD, VMOVAPS, VMOVDQA32, and VMOVDQA64 when encoded
 *        with EVEX.128 and k0 (masking disabled).
 *    Note that these instructions require the linear addresses of their
 *    memory operands to be 16-byte aligned.
 *
 * We do not yet have a similar guarantee from AMD, so we detect this
 * at runtime rather than assuming the fact when __AVX__ is defined.
 */
bool have_atomic128;

static void __attribute__((constructor))
init_have_atomic128(void)
{
    unsigned int a, b, c, d, xcrl, xcrh;

    __cpuid(0, a, b, c, d);
    if (a < 1) {
        return; /* AVX leaf not present */
    }
    if (c != signature_INTEL_ecx) {
        return; /* Not an Intel product */
    }

    __cpuid(1, a, b, c, d);
    if ((c & (bit_AVX | bit_OSXSAVE)) != (bit_AVX | bit_OSXSAVE)) {
        return; /* AVX not present or XSAVE not enabled by OS */
    }

    /*
     * The xgetbv instruction is not available to older versions of
     * the assembler, so we encode the instruction manually.
     */
    asm(".byte 0x0f, 0x01, 0xd0" : "=a" (xcrl), "=d" (xcrh) : "c" (0));
    if ((xcrl & 6) != 6) {
        return; /* AVX not enabled by OS */
    }

    have_atomic128 = true;
}
#endif /* __x86_64__ */
