#include <stdint.h>
#include <unistd.h>

int main(void)
{
    uint64_t data = 0x000000000025594cull;
    uint64_t result = 0;

    asm volatile(
        "    cvb %[result],%[data]\n"
        : [result] "+r" (result)
        : [data] "m" (data));
    if (result != 0x63fa) {
        write(1, "bad result\n", 11);
        return 1;
    }
    return 0;
}
