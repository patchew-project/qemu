/*
 * Purpose: test that writes of a register in a packet are performed only after
 * that packet has finished its execution.
 */

    .text
    .globl _start

_start:
    {
        allocframe(#8)
    }
    {
        r2 = #4
        r3 = #6
    }
    {
        memw(sp+#0) = r2
    }
    {
        r3 = memw(sp+#0)
        r0 = add(r2, r3)
    }
    {
        deallocframe
        p0 = cmp.eq(r3, #4)
        p0 = cmp.eq(r0, #10); if (p0.new) jump:t .Lcorner_cases
        jump fail
    }

/* Scalar and pair writes commit together, including reads of pair halves. */
.Lcorner_cases:
    { r0 = #11; r1 = #22; r2 = #33; r3 = #44 }
    { r0 = r1; r1 = r2; r2 = r0 }
    { p0 = cmp.eq(r0, #22); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r1, #33); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r2, #11); if (!p0.new) jump:nt fail }
    { r1:0 = r3:2; r3:2 = r1:0 }
    { p0 = cmp.eq(r0, #11); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r1, #44); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r2, #22); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r3, #33); if (!p0.new) jump:nt fail }

    { r0.h = #0x1234; r5:4 = r1:0 }
    { p0 = cmp.eq(r4, #11); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r5, #44); if (!p0.new) jump:nt fail }
    { r6 = ##0x1234000b }
    { p0 = cmp.eq(r0, r6); if (!p0.new) jump:nt fail }

/* An accumulator reads its old destination, as does a parallel copy. */
    { r0 = #5; r1 = #3; r2 = #7 }
    { r0 += mpyi(r1, r2); r3 = r0 }
    { p0 = cmp.eq(r0, #26); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r3, #5); if (!p0.new) jump:nt fail }

/* A load does not forward to an ordinary store's data or base operands. */
    { allocframe(#16) }
    { r0 = #17; r1 = #29; r20 = sp }
    { memw(sp+#0) = r1 }
    { r0 = memw(r20+#0); memw(r20+#4) = r0 }
    { r2 = memw(sp+#4) }
    { p0 = cmp.eq(r0, #29); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r2, #17); if (!p0.new) jump:nt fail }
/* Without :mem_noshuf, an aliasing load sees memory before the store. */
    { r0 = memw(r20+#0); memw(r20+#0) = r2 }
    { p0 = cmp.eq(r0, #29); if (!p0.new) jump:nt fail }
    { r4 = memw(r20+#0) }
    { p0 = cmp.eq(r4, #17); if (!p0.new) jump:nt fail }
    { r0 = memw(r20++#4); r3 = r20 }
    { p0 = cmp.eq(r3, sp); if (!p0.new) jump:nt fail }
    { r4 = sub(r20, sp) }
    { p0 = cmp.eq(r4, #4); if (!p0.new) jump:nt fail }
    { deallocframe; jump pass }
