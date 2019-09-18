#include "qemu/osdep.h"
#include "fork_fuzz.h"

uintptr_t feature_shm;

void counter_shm_init(void)
{
    feature_shm = (uintptr_t)mmap(NULL,
            &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START,
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    return;
}

void counter_shm_store(void)
{
    memcpy((void *)feature_shm,
            &__FUZZ_COUNTERS_START,
            &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START);
}

void counter_shm_load(void)
{
    memcpy(&__FUZZ_COUNTERS_START,
            (void *)feature_shm,
            &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START);
}

