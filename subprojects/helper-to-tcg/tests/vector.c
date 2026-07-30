#include <stdint.h>

__attribute__((annotate("ptr-to-offset: 0"))) void
helper_vec_splat_reg(void *restrict d, uint8_t imm)
{
    for (int i = 0; i < 32; ++i) {
        ((uint8_t *)d)[i] = imm;
    }
}

__attribute__((annotate("immediate: 1")))
__attribute__((annotate("ptr-to-offset: 0"))) void
helper_vec_splat_imm(void *restrict d, uint8_t imm)
{
    for (int i = 0; i < 32; ++i) {
        ((uint8_t *)d)[i] = imm;
    }
}

__attribute__((annotate("ptr-to-offset: 0, 1, 2"))) void
helper_vec_add(void *restrict d, void *restrict a, void *restrict b)
{
    for (int i = 0; i < 32; ++i) {
        ((uint8_t *)d)[i] = ((uint8_t *)a)[i] + ((uint8_t *)b)[i];
    }
}

__attribute__((annotate("ptr-to-offset: 0, 1, 2"))) void
helper_vec_add32(void *restrict d, void *restrict a, void *restrict b)
{
    for (int i = 0; i < 32; ++i) {
        ((uint32_t *)d)[i] = ((uint32_t *)a)[i] + ((uint32_t *)b)[i];
    }
}
