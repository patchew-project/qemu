#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>


int main()
{
    /*
     * ## 1. RISC-V machine code.
     * Assembly:
     *   L: j L          ; Jump to self (spin).
     *   li a0, 42       ; Place 42 into the return value register a0.
     *   ret             ; Return to caller.
     */
    static const uint32_t machine_code[] = {
        0x0000006f, /* jal zero, #0 */
        0x02a00513, /* addi a0, zero, 42 */
        0x00008067  /* jalr zero, ra, 0 */
    };
    size_t code_size = sizeof(machine_code);
    int tmp;
    pthread_t thread_id;
    void *thread_return_value;
    uint32_t *buffer;

    /* ## 2. Allocate executable memory. */
    buffer = mmap(
        NULL,
        code_size,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0
    );
    assert(buffer != MAP_FAILED);

    /* ## 3. Copy machine code into buffer. */
    memcpy(buffer, machine_code, code_size);

    /* ## 4. Execute the code in a separate thread. */
    tmp = pthread_create(&thread_id, NULL, (void *(*)(void *))buffer, NULL);
    assert(tmp == 0);

    /*
     * Wait a second and then try to patch the generated code to get the
     * runner thread to get unstuck by patching the spin jump.
     */
    sleep(1);
    buffer[0] = 0x00000013;  /* nop */
    __builtin___clear_cache((char *)buffer, (char *)(buffer + 1));

    tmp = pthread_join(thread_id, &thread_return_value);
    assert(tmp == 0);

    tmp = (intptr_t)thread_return_value;
    assert(tmp == 42);
    return 0;
}
