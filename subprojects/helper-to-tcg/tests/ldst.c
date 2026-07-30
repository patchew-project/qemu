#include <stdint.h>

/* Opaque CPU state type, will be mapped to tcg_env */
struct CPUArchState;
typedef struct CPUArchState CPUArchState;

/* Prototype of QEMU helper guest load/store functions, see exec/cpu_ldst.h */
uint32_t cpu_ldub_data(CPUArchState *, uint32_t ptr);
void cpu_stb_data(CPUArchState *, uint32_t ptr, uint32_t data);

uint32_t helper_ld8(CPUArchState *env, uint32_t addr) {
    return cpu_ldub_data(env, addr);
}

void helper_st8(CPUArchState *env, uint32_t addr, uint32_t data) {
    return cpu_stb_data(env, addr, data);
}
