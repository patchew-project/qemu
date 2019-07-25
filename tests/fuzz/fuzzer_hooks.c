#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "fuzzer_hooks.h"

#include <dlfcn.h>
#include <elf.h>


extern void* _ZN6fuzzer3TPCE;
// The libfuzzer handlers
void __real___sanitizer_cov_8bit_counters_init(uint8_t*, uint8_t*);
void __real___sanitizer_cov_trace_pc_guard_init(uint8_t*, uint8_t*);

void __wrap___sanitizer_cov_8bit_counters_init(uint8_t *Start, uint8_t *Stop);
void __wrap___sanitizer_cov_trace_pc_guard_init(uint8_t *Start, uint8_t *Stop);


void* counter_shm;

typedef struct CoverageRegion {
    uint8_t* start;
    size_t length;
    bool store; /* Set this if it needs to be copied to the forked process */
} CoverageRegion;

CoverageRegion regions[10];
int region_index = 0;

void __wrap___sanitizer_cov_8bit_counters_init(uint8_t *Start, uint8_t *Stop)
{
    regions[region_index].start = Start;
    regions[region_index].length = Stop-Start;
    regions[region_index].store = true;
    region_index++;
    __real___sanitizer_cov_8bit_counters_init(Start, Stop);
}

void __wrap___sanitizer_cov_trace_pc_guard_init(uint8_t *Start, uint8_t *Stop)
{
    regions[region_index].start = Start;
    regions[region_index++].length = Stop-Start;
    regions[region_index].store = true;
    region_index++;
    __real___sanitizer_cov_trace_pc_guard_init(Start, Stop);
}

static void add_tpc_region(void)
{
    /* Got symbol and length from readelf. Horrible way to do this! */
    regions[region_index].start = (uint8_t*)(&_ZN6fuzzer3TPCE);
    regions[region_index].length = 0x443c00; 
    regions[region_index].store = true;
    region_index++;
}

void counter_shm_init(void)
{
    /*
     * Add the  internal libfuzzer object that gets modified by cmp, etc
     * callbacks
     */
    add_tpc_region(); 

    size_t length = 0;
    for(int i=0; i<region_index; i++){
        printf("%d %lx\n", i, length);
        length += regions[i].length;
    }

    /* 
     * Map some shared memory. When we use a fork-server we can copy the
     * libfuzzer-related counters
     * */
    counter_shm = mmap(NULL, length, PROT_READ | PROT_WRITE, 
            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(counter_shm == MAP_FAILED) {
        printf("mmap() failed\n");
        do { perror("error:"); exit(EXIT_FAILURE); } while (0);
        exit(-1);
    }
}

void counter_shm_store(void)
{
    size_t offset = 0;
    for(int i=0; i<region_index; i++) {
        if(regions[i].store) {
            memcpy(counter_shm + offset, regions[i].start, regions[i].length);
        }
        offset+=regions[i].length;
    }
}

void counter_shm_load(void)
{
    size_t offset = 0;
    for(int i=0; i<region_index; i++) {
        if(regions[i].store) {
            memcpy(regions[i].start, counter_shm + offset, regions[i].length);
        }
        offset+=regions[i].length;
    }
}

