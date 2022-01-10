#include <stdint.h>
#include <unistd.h>

int main(void)
{
    uint64_t op1 = 0xb90281a3105939dfull;
    uint64_t op2 = 0xb5e4df7e082e4c5eull;
    uint64_t cc = 0xffffffffffffffffull;

    asm("sll\t%[op1],0xd04(%[op2])"
        "\n\tipm\t%[cc]"
        : [op1] "+r" (op1),
          [cc] "+r" (cc)
        : [op2] "r" (op2)
        : "cc");
    if (op1 != 0xb90281a300000000ull) {
        write(1, "bad result\n", 11);
        return 1;
    }
    if (cc != 0xffffffff10ffffffull) {
        write(1, "bad cc\n", 7);
        return 1;
    }
    return 0;
}
