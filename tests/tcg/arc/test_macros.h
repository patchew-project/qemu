#ifndef __TEST_MACROS_SCALAR_H
#define __TEST_MACROS_SCALAR_H

#ifdef ARCTEST_ARC32
#define __arc_xlen 32
#else
#define __arc_xlen 64
#endif

#define xstr(a) str(a)
#define str(a) #a

#define MASK_XLEN(x) ((x) & ((1 << (__arc_xlen - 1) << 1) - 1))
#define SEXT_IMM(x) ((x) | (-(((x) >> 11) & 1) << 11))

#define TEST_CASE( testnum, testreg, correctval, name, code... ) \
    test_ ## testnum:                                       \
    code`                                                   \
    mov  r12, testnum`                                      \
    sub.f 0,testreg, correctval`                            \
    bne  @fail`                                             \
    PASS_TEST(name)

#define TEST_IMM_OP( testnum, inst, result, val1, imm ) \
    TEST_CASE( testnum, r0, result, xstr(inst) ":" xstr(testnum),       \
               mov  r1, MASK_XLEN(val1)`                \
               inst r0, r1, SEXT_IMM(imm)               \
        )

#define TEST_RR_3OP( testnum, inst, result, val1, val2 ) \
    TEST_CASE( testnum, r0, result, xstr(inst) ":" xstr(testnum),  \
               mov  r1, MASK_XLEN(val1)`                \
               mov  r2, MASK_XLEN(val2)`                \
               inst r0, r1, r2                          \
        )

#define TEST_RR_2OP( testnum, inst, result, val)         \
    TEST_CASE( testnum, r0, result, xstr(inst) ":" xstr(testnum),  \
               mov  r1, MASK_XLEN(val)`                 \
               inst r0, r1                              \
        )

#define TEST_IMM_SRC1_EQ_DEST( testnum, inst, result, val1, imm ) \
    TEST_CASE( testnum, r1, result, xstr(inst) ":" xstr(testnum),           \
               mov  r1, MASK_XLEN(val1)`                          \
               inst r1, r1, SEXT_IMM(imm)                         \
        )

#define TEST_RR_SRC1_EQ_DEST( testnum, inst, result, val1, val2 ) \
    TEST_CASE( testnum, r1, result, xstr(inst) ":" xstr(testnum),           \
               mov  r1, MASK_XLEN(val1)`                          \
               mov  r2, MASK_XLEN(val2)`                          \
               inst r1, r1, r2                                    \
        )

#define TEST_RR_2OP_SRC1_EQ_DEST( testnum, inst, result, val )     \
    TEST_CASE( testnum, r1, result, xstr(inst) ":" xstr(testnum),            \
               mov  r1, MASK_XLEN(val)`                            \
               inst r1, r1                                         \
        )

#define TEST_RR_SRC2_EQ_DEST( testnum, inst, result, val1, val2 )  \
    TEST_CASE( testnum, r2, result, xstr(inst) ":" xstr(testnum),            \
               mov  r1, MASK_XLEN(val1)`                           \
               mov  r2, MASK_XLEN(val2)`                           \
               inst r2, r1, r2                                     \
        )

#define TEST_RR_SRC12_EQ_DEST( testnum, inst, result, val1 ) \
    TEST_CASE( testnum, r1, result, xstr(inst) ":" xstr(testnum),      \
               mov  r1, MASK_XLEN(val1)`                     \
               inst r1, r1, r1                               \
        )

#define TEST_2OP_CARRY( testnum, inst, expected, val1, val2) \
    test_ ## testnum:                                        \
    mov  r12, testnum`                                       \
        mov  r1, MASK_XLEN(val1)`                            \
        mov  r2, MASK_XLEN(val2)`                            \
        inst.f   0, r1, r2`                                  \
        mov.cs   r3,(~expected) & 0x01`                      \
        mov.cc   r3, (expected) & 0x01`                      \
        cmp      r3, 0`                                      \
        bne      @fail

#define TEST_1OP_CARRY( testnum, inst, expected, val) \
    test_ ## testnum:                                 \
    mov  r12, testnum`                                \
        add.f    0, r0, r0`                           \
        mov      r1, MASK_XLEN(val)`                  \
        inst.f   0, r1`                               \
        mov.cs   r3,(~expected) & 0x01`               \
        mov.cc   r3, (expected) & 0x01`               \
        cmp      r3, 0`                               \
        bne      @fail

#define TEST_2OP_ZERO( testnum, inst, expected, val1, val2)  \
    test_ ## testnum:                                        \
    mov  r12, testnum`                                       \
        mov      r1, MASK_XLEN(val1)`                        \
        mov      r2, MASK_XLEN(val2)`                        \
        inst.f   0, r1, r2`                                  \
        mov.eq   r3, (~expected) & 0x01`                     \
        mov.ne   r3, (expected) & 0x01`                      \
        cmp      r3, 0`                                      \
        bne      @fail

#define TEST_1OP_ZERO( testnum, inst, expected, val)  \
    test_ ## testnum:                                 \
    mov  r12, testnum`                                \
        add.f    0, r0, r0`                           \
        mov      r1, MASK_XLEN(val)`                  \
        inst.f   0, r1`                               \
        mov.eq   r3, (~expected) & 0x01`              \
        mov.ne   r3, (expected) & 0x01`               \
        cmp      r3, 0`                               \
        bne      @fail

#define TEST_2OP_OVERFLOW( testnum, inst, expected, val1, val2) \
    test_ ## testnum:                                           \
    mov  r12, testnum`                                          \
        mov      r1, MASK_XLEN(val1)`                           \
        mov      r2, MASK_XLEN(val2)`                           \
        inst.f   0, r1, r2`                                     \
        mov.vs   r3,(~expected) & 0x01`                         \
        mov.vc   r3, (expected) & 0x01`                         \
        cmp      r3, 0`                                         \
        bne      @fail

#define TEST_1OP_OVERFLOW( testnum, inst, expected, val) \
    test_ ## testnum:                                    \
    mov  r12, testnum`                                   \
        add.f    0, r0, r0`                              \
        mov      r1, MASK_XLEN(val)`                     \
        inst.f   0, r1`                                  \
        mov.vs   r3,(~expected) & 0x01`                  \
        mov.vc   r3, (expected) & 0x01`                  \
        cmp      r3, 0`                                  \
        bne      @fail

#define TEST_2OP_NEGATIVE( testnum, inst, expected, val1, val2)    \
    test_ ## testnum:                                              \
    mov  r12, testnum`                                             \
        mov      r1, MASK_XLEN(val1)`                              \
        mov      r2, MASK_XLEN(val2)`                              \
        inst.f   0, r1, r2`                                        \
        mov.mi   r3,(~expected) & 0x01`                            \
        mov.pl   r3, (expected) & 0x01`                            \
        cmp      r3, 0`                                            \
        bne      @fail

#define TEST_1OP_NEGATIVE( testnum, inst, expected, val)    \
    test_ ## testnum:                                       \
    mov  r12, testnum`                                      \
        add.f    0, r0, r0`                                 \
        mov      r1, MASK_XLEN(val)`                        \
        inst.f   0, r1`                                     \
        mov.mi   r3,(~expected) & 0x01`                     \
        mov.pl   r3, (expected) & 0x01`                     \
        cmp      r3, 0`                                     \
        bne      @fail


#endif

#define TEST_BR2_OP_TAKEN( testnum, inst, val1, val2 )  \
    test_ ## testnum:`                                  \
        mov  r12, testnum`                              \
        mov  r1, val1`                                  \
        mov  r2, val2`                                  \
        sub.f 0,r1,r2`                                  \
        inst 1f`                                        \
        b @fail`                                        \
        1:

#define TEST_BR2_OP_NOTTAKEN( testnum, inst, val1, val2 ) \
    test_ ## testnum:`                                    \
    mov  r12,testnum`                                     \
        mov  r1, val1`                                    \
        mov  r2, val2`                                    \
        sub.f 0,r1,r2`                                    \
        inst @fail

#define TEST_BR_OP_TAKEN( testnum, inst, val1, val2 )  \
    test_ ## testnum:`                                  \
        mov  r12, testnum`                              \
        mov  r1, val1`                                  \
        mov  r2, val2`                                  \
        inst r1,r2,1f`                                  \
        b @fail`                                        \
        1:

#define TEST_BR_OP_NOTTAKEN( testnum, inst, val1, val2 ) \
    test_ ## testnum:`                                    \
        mov  r12,testnum`                                 \
        mov  r1, val1`                                    \
        mov  r2, val2`                                    \
        inst r1,r2,@fail

#define ARCTEST_BEGIN \
    .text`                                      \
        .align 4 `                              \
        .global main`                           \
    main:                                       \
        test_1:`                                \
        mov r12,1`                              \
        mov.f 0,0`                              \
        bne @fail

#define ARCTEST_END \
         .align 4 ` \
1:`\
        st 1,[0xf0000008]`\
        b @1b`\
fail:`\
        mov     r2, '['`\
        st      r2, [0x90000000]`\
        mov     r2, 'F'`\
        st      r2, [0x90000000]`\
        mov     r2, 'a'`\
        st      r2, [0x90000000]`\
        mov     r2, 'i'`\
        st      r2, [0x90000000]`\
        mov     r2, 'l'`\
        st      r2, [0x90000000]`\
        mov     r2, ']'`\
        st      r2, [0x90000000]`\
        mov     r2, ' '`\
        st      r2, [0x90000000]`\
        mov r13, r12`\
        mov r15, 0x30`\
        mov r14, r12`\
loop_z: `\
        sub.f   r13, r13, 0x0A`\
        add.pl  r15, r15, 1`\
        mov.pl  r14, r13 `\
        bpl     @loop_z`\
        st      r15, [0x90000000]`\
        add     r14, r14, 0x30`\
        st      r14, [0x90000000]`\
        mov     r2, '\n'`\
        st      r2, [0x90000000]`\
        b       1b`

#define PASS_TEST(name)\
    .data `            \
2010:`\
    .ascii "[PASS] ",name ,"\n\0"` \
  .align 4`\
  .text`\
  mov_s     r11, @2010b`\
  1010:`\
    ldb.ab  r12, [r11, 1]`\
    breq    r12, 0, @1011f`\
    stb     r12, [0x90000000]`\
    j       @1010b`\
  1011:`
