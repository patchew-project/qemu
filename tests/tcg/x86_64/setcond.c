#include <stdint.h>
#include <assert.h>

uint8_t test(uint8_t a)
{
    uint8_t res = 0xff;
    asm(
        "lea -0x1160(%%edi), %%edx\n\t"
        "lea -0xd7b0(%%edi), %%ecx\n\t"
        "cmp $0x9f, %%edx\n\t"
        "setbe %%dl\n\t"
        "cmp $0x4f, %%ecx\n\t"
        "setbe %%cl\n\t"
        "or %%ecx, %%edx\n\t"
        "cmp $0x200b, %%edi\n\t"
        "sete %0\n\t"
        : "=r"(res)
    );
    return res;
}

int main(void)
{
    for (uint8_t a = 0; a < 0xff; a++) {
        assert(test(a) == 0);
    }
    return 0;
}
