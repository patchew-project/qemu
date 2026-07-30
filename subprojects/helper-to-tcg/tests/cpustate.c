#include <stddef.h>
#include <stdint.h>

#define stringify(str) #str

#include "tcg-global-mappings.h"

typedef struct SpecialData {
    uint32_t a;
    uint32_t unmapped_field;
} SpecialData;

typedef struct CPUArchState {
    uint32_t regs[32];
    uint32_t unmapped_field;
    SpecialData data[8];
    uint32_t mapped_field;
} CPUArchState;

/* Dummy struct, in QEMU this would correspond to TCGv_i32 in tcg.h */
/* Global TCGv's representing CPU state */
struct{} tcg_regs[32];
struct{} tcg_a[8];
struct{} tcg_field;

cpu_tcg_mapping mappings[] = {
    CPU_TCG_MAP_ARRAY(CPUArchState, tcg_regs, regs, NULL),
    CPU_TCG_MAP_ARRAY_OF_STRUCTS(CPUArchState, tcg_a, data, a, NULL),
    CPU_TCG_MAP(CPUArchState, tcg_field, mapped_field),
};

__attribute__((annotate("immediate: 1"))) uint32_t helper_reg(CPUArchState *env,
                                                              uint32_t i) {
    return env->regs[i];
}

__attribute__((annotate("immediate: 1"))) uint32_t
helper_data_a(CPUArchState *env, uint32_t i) {
    return env->data[i].a;
}

uint32_t helper_single_mapped(CPUArchState *env) { return env->mapped_field; }

uint32_t helper_unmapped(CPUArchState *env) { return env->unmapped_field; }
