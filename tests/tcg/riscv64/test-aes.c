/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../multiarch/test-aes-main.c.inc"

bool test_SB_SR(uint8_t *o, const uint8_t *i)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;

    asm("aes64es %0,%2,%3\n\t"
        "aes64es %1,%3,%2"
        : "=&r"(o8[0]), "=&r"(o8[1]) : "r"(i8[0]), "r"(i8[1]));
    return true;
}

bool test_MC(uint8_t *o, const uint8_t *i)
{
    return false;
}

bool test_SB_SR_MC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;
    const uint64_t *k8 = (const uint64_t *)k;

    asm("aes64esm %0,%2,%3\n\t"
        "aes64esm %1,%3,%2\n\t"
        "xor %0,%0,%4\n\t"
        "xor %1,%1,%5"
        : "=&r"(o8[0]), "=&r"(o8[1])
        : "r"(i8[0]), "r"(i8[1]), "r"(k8[0]), "r"(k8[1]));
    return true;
}

bool test_ISB_ISR(uint8_t *o, const uint8_t *i)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;

    asm("aes64ds %0,%2,%3\n\t"
        "aes64ds %1,%3,%2"
        : "=&r"(o8[0]), "=&r"(o8[1]) : "r"(i8[0]), "r"(i8[1]));
    return true;
}

bool test_IMC(uint8_t *o, const uint8_t *i)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;

    asm("aes64im %0,%0\n\t"
        "aes64im %1,%1"
        : "=r"(o8[0]), "=r"(o8[1]) : "0"(i8[0]), "1"(i8[1]));
    return true;
}

bool test_ISB_ISR_AK_IMC(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    return false;
}

bool test_ISB_ISR_IMC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k)
{
    uint64_t *o8 = (uint64_t *)o;
    const uint64_t *i8 = (const uint64_t *)i;
    const uint64_t *k8 = (const uint64_t *)k;

    asm("aes64dsm %0,%2,%3\n\t"
        "aes64dsm %1,%3,%2\n\t"
        "xor %0,%0,%4\n\t"
        "xor %1,%1,%5"
        : "=&r"(o8[0]), "=&r"(o8[1])
        : "r"(i8[0]), "r"(i8[1]), "r"(k8[0]), "r"(k8[1]));
    return true;
}
