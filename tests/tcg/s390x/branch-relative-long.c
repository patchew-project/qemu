#include <assert.h>
#include <stddef.h>
#include <sys/mman.h>

int main(void)
{
    const unsigned short opcodes[] = {
        0xc005,  /* brasl %r0 */
        0xc0f4,  /* brcl 0xf */
    };
    size_t length = 0x100000006;
    unsigned char *buf;
    int i;

    buf = mmap(NULL, length, PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(buf != MAP_FAILED);

    *(unsigned short *)&buf[0] = 0x07fe;  /* br %r14 */
    *(unsigned int *)&buf[0x100000002] = 0x80000000;
    for (i = 0; i < sizeof(opcodes) / sizeof(opcodes[0]); i++) {
        *(unsigned short *)&buf[0x100000000] = opcodes[i];
        ((void (*)(void))&buf[0x100000000])();
    }

    munmap(buf, length);

    return 0;
}
