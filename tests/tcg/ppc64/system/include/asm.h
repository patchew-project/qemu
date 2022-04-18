#ifndef PPC64_ASM_H
#define PPC64_ASM_H

#define XCONCAT(a, b)       a ## b
#define CONCAT(a, b)        XCONCAT(a, b)

/* Load an immediate 64-bit value into a register */
#define LOAD_IMM64(r, e)                        \
    lis     r, (e)@highest;                     \
    ori     r, r, (e)@higher;                   \
    rldicr  r, r, 32, 31;                       \
    oris    r, r, (e)@h;                        \
    ori     r, r, (e)@l;

/* Switch CPU to little-endian mode, if needed */
#define FIXUP_ENDIAN \
    tdi   0, 0, 0x48;   /* Reverse endian of b . + 8 */             \
    b     $ + 44;       /* Skip trampoline if endian is good */     \
    .long 0xa600607d;   /* mfmsr r11 */                             \
    .long 0x01006b69;   /* xori r11,r11,1 */                        \
    .long 0x00004039;   /* li r10,0 */                              \
    .long 0x6401417d;   /* mtmsrd r10,1 */                          \
    .long 0x05009f42;   /* bcl 20,31,$+4 */                         \
    .long 0xa602487d;   /* mflr r10 */                              \
    .long 0x14004a39;   /* addi r10,r10,20 */                       \
    .long 0xa6035a7d;   /* mtsrr0 r10 */                            \
    .long 0xa6037b7d;   /* mtsrr1 r11 */                            \
    .long 0x2400004c    /* rfid */

/* Handle differences between ELFv1 and ELFv2 ABIs */

#define DOT_LABEL(name)     CONCAT(., name)

#if !defined(_CALL_ELF) || _CALL_ELF == 1
#define FUNCTION(name)                          \
    .section ".opd", "aw";                      \
    .p2align 3;                                 \
    .globl   name;                              \
name:                                           \
    .quad   DOT_LABEL(name), .TOC.@tocbase, 0;  \
    .previous;                                  \
DOT_LABEL(name):

#define CALL(fn)                                \
    LOAD_IMM64(%r12, fn);                       \
    ld      %r12, 0(%r12);                      \
    mtctr   %r12;                               \
    bctrl

#define CALL_LOCAL(fn)                          \
    bl      DOT_LABEL(fn)

#else
#define FUNCTION(name)                          \
    .globl   name;                              \
name:

#define CALL(fn)                                \
    LOAD_IMM64(%r12, fn);                       \
    mtctr   %r12;                               \
    bctrl

#define CALL_LOCAL(fn)                          \
    bl      fn

#endif

#endif
