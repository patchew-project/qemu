#include <stdint.h>


#define F_EPI "stg %%r0, %[res] " : [res] "+m" (res) : : "r0", "r2", "r3"

#define F_PRO    asm ( \
    "lg %%r2, %[a]\n"  \
    "lg %%r3, %[b]\n"  \
    "lg %%r0, %[c]\n"  \
    "ltgr %%r0, %%r0"  \
    : : [a] "m" (a),   \
        [b] "m" (b),   \
        [c] "m" (c)    \
    : "r0", "r2", "r3", "r4")



#define Fi3(S, ASM) uint64_t S(uint64_t a, uint64_t b, uint64_t c) \
{ uint64_t res = 0; F_PRO ; ASM ; return res; }


Fi3 (_selre,     asm("selre    %%r0, %%r3, %%r2\n" F_EPI))
Fi3 (_selgrz,    asm("selgrz   %%r0, %%r3, %%r2\n" F_EPI))
Fi3 (_selfhrnz,  asm("selfhrnz %%r0, %%r3, %%r2\n" F_EPI))


int main(int argc, char *argv[])
{
    uint64_t a = ~0, b = ~0, c = ~0;
    a =    _selre(0x066600000066ull, 0x066600000006ull, a);
    b =   _selgrz(0xF00D00000005ull, 0xF00D00000055ull, b);
    c = _selfhrnz(0x004400000044ull, 0x000400000004ull, c);

    if ((0xFFFFFFFF00000066ull != a) ||
        (0x0000F00D00000005ull != b) ||
        (0x00000004FFFFFFFFull != c))
    {
        return 1;
    }
    return 0;
}

