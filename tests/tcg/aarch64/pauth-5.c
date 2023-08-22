#include <assert.h>
#include <sys/auxv.h>

static int x;

int main()
{
    int *p0 = &x, *p1, *p2, *p3;
    unsigned long salt = 0;
    unsigned long isar1, isar2;
    int pac_feature;

    assert(getauxval(AT_HWCAP) & HWCAP_CPUID);

    asm("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
    asm("mrs %0, id_aa64isar2_el1" : "=r"(isar2));

    pac_feature = ((isar1 >> 4) & 0xf)   /* APA */
                | ((isar1 >> 8) & 0xf)   /* API */
                | ((isar2 >> 12) & 0xf); /* APA3 */

    /*
     * Exit if no PAuth or FEAT_FPAC, which will SIGILL on AUTDA failure
     * rather than return an error for us to check below.
     */
    if (pac_feature == 0 || pac_feature >= 4) {
        return 0;
    }

    /*
     * With TBI enabled and a 48-bit VA, there are 7 bits of auth, and so
     * a 1/128 chance of auth = pac(ptr,key,salt) producing zero.
     * Find a salt that creates auth != 0.
     */
    do {
        salt++;
        asm("pacda %0, %1" : "=r"(p1) : "r"(salt), "0"(p0));
    } while (p0 == p1);

    /*
     * This pac must fail, because the input pointer bears an encryption,
     * and so is not properly extended within bits [55:47].  This will
     * toggle bit 54 in the output...
     */
    asm("pacda %0, %1" : "=r"(p2) : "r"(salt), "0"(p1));

    /* ... so that the aut must fail, setting bit 53 in the output ... */
    asm("autda %0, %1" : "=r"(p3) : "r"(salt), "0"(p2));

    /* ... which means this equality must not hold. */
    assert(p3 != p0);
    return 0;
}
