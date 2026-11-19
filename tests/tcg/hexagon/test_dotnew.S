/* Purpose: test the .new operator while performing memory stores. */

    .text
    .globl _start

_start:
    {
        allocframe(#16)
    }
    {
        r0 = #1
        memw(sp+#0) = r0.new
    }
    {
        r1 = #2
        memw(sp+#4) = r1.new
    }
    {
        r2 = #3
        memw(sp+#8) = r2.new
    }
    {
        r0 = memw(sp+#8)
    }
    {
        r1 = memw(sp+#4)
    }
    {
        r2 = memw(sp+#0)
    }
    {
        r3 = mpyi(r1, r2)
    }
    {
        deallocframe
        p0 = cmp.eq(r3, #2); if (p0.new) jump:t .Lcorner_cases
        jump fail
    }

/*
 * .new selects a producer through an unrelated instruction and an extender.
 * The parallel ordinary read still sees the producer's input value.
 */
.Lcorner_cases:
    { allocframe(#16) }
    { r0 = #7; r1 = #11 }
    {
        r0 = ##0x12345678
        r3 = r0
        memw(sp+#0) = r0.new
    }
    { r4 = memw(sp+#0); r5 = ##0x12345678 }
    { p0 = cmp.eq(r4, r5); if (!p0.new) jump:nt fail }
    { p0 = cmp.eq(r3, #7); if (!p0.new) jump:nt fail }

/* A load producer must forward the loaded value, not its old destination. */
    { r0 = #37 }
    { r0 = memw(sp+#0); memw(sp+#4) = r0.new }
    { r4 = memw(sp+#4) }
    { p0 = cmp.eq(r4, r5); if (!p0.new) jump:nt fail }

/* Predicate.new gates the store independently of the data.new forwarding. */
    { r0 = #13; r1 = #0; memw(sp+#8) = #19 }
    { p0 = cmp.eq(r1, #0); r0 = #23; if (p0.new) memw(sp+#8) = r0.new }
    { r4 = memw(sp+#8) }
    { p0 = cmp.eq(r4, #23); if (!p0.new) jump:nt fail }
    { p0 = cmp.gt(r1, #0); r0 = #31; if (p0.new) memw(sp+#8) = r0.new }
    { r4 = memw(sp+#8) }
    { p0 = cmp.eq(r4, #23); if (!p0.new) jump:nt fail }
    { deallocframe; jump pass }
