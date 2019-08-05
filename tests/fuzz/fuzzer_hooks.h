#ifndef FUZZER_HOOKS_H
#define FUZZER_HOOKS_H


/* NOTE: Pending https://reviews.llvm.org/D65672
 * Alternatively, a similar functionality can be added fairly straightforwardly
 * with AFL deferred fork mode, albeit requiring a different fuzzer and compiler
 * https://github.com/mirrorer/afl/blob/master/llvm_mode/README.llvm#L82
 */
extern void LLVMFuzzerIterateFeatureRegions(void (*CB)(void *, size_t));

void measure_shm_size(void *start, size_t len);

void counter_shm_init(void);
void counter_shm_store(void);
void counter_shm_load(void);
void feature_load(void *start, size_t len);
void feature_store(void *start, size_t len);

#endif

