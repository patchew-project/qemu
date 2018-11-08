/*
 * Test DMULT.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

struct hi_lo { int64_t hi; uint64_t lo; };

static struct hi_lo dmult(int64_t rs, int64_t rt)
{
    int64_t hi;
    uint64_t lo;

    /*
     * The R5900 reports itself as MIPS III but does not implement DMULT.
     * Verify that DMULT is emulated properly in user mode.
     */
    __asm__ __volatile__ (
            "    .set  mips3\n"
            "    dmult %2, %3\n"
            "    mfhi  %0\n"
            "    mflo  %1\n"
            : "=r" (hi), "=r" (lo)
            : "r" (rs), "r" (rt));

    return (struct hi_lo) { .hi = hi, .lo = lo };
}

int main()
{
    /* Verify that multiplying two 64-bit numbers yields a 128-bit number. */
    struct hi_lo r = dmult(2760727302517, 5665449960167);

    assert(r.hi == 847887);
    assert(r.lo == 7893651516417804947);

    return 0;
}
