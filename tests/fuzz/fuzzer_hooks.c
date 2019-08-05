#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "fuzzer_hooks.h"

#include <dlfcn.h>
#include <elf.h>



void *counter_shm;
size_t feature_shm_len;
uintptr_t feature_shm;
size_t offset;

typedef struct CoverageRegion {
    uint8_t *start;
    size_t length;
    bool store; /* Set this if it needs to be copied to the forked process */
} CoverageRegion;

CoverageRegion regions[10];
int region_index;


void counter_shm_init(void)
{
    LLVMFuzzerIterateFeatureRegions(&measure_shm_size);
    feature_shm = (uintptr_t)mmap(NULL, feature_shm_len,
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

void counter_shm_store(void)
{
    offset = 0;
    LLVMFuzzerIterateFeatureRegions(&feature_store);
}

void counter_shm_load(void)
{
    offset = 0;
    LLVMFuzzerIterateFeatureRegions(&feature_load);
}

void feature_load(void *start, size_t len)
{
    memcpy(start, (void *)(feature_shm + offset), len);
    offset += len;
}

void feature_store(void *start, size_t len)
{
    memcpy((void *)(feature_shm + offset), start, len);
    offset += len;
}

void measure_shm_size(void *start, size_t len)
{
    feature_shm_len += len;
}

