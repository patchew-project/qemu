#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "translate.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg.h"
#include "tcg/tcg-global-mappings.h"
#include "exec/translation-block.h"
#include "vector-layout-f64M.h"

#define HELPER_H "helper-to-tcg-support-helpers.h"
#include "exec/helper-proto-common.h"
#include "exec/helper-proto.h.inc"
#include "exec/helper-gen-common.h"
#include "exec/helper-gen.h.inc"

inline size_t ind_8(size_t i) {
const size_t c = i / 8;
const size_t b = c / 8;
const size_t ci = i % 8;
const size_t bc = c % 8;
return 64 * b + 8 * (7 - bc) + (7 - ci);
}

inline size_t ind_16(size_t i) {
const size_t c = i / 4;
const size_t b = c / 8;
const size_t ci = i % 4;
const size_t bc = c % 8;
return 32 * b + 4 * (7 - bc) + (3 - ci);
}

inline size_t ind_32(size_t i) {
const size_t c = i / 2;
const size_t b = c / 8;
const size_t ci = i % 2;
const size_t bc = c % 8;
return 16 * b + 2 * (7 - bc) + (1 - ci);
}

inline size_t ind_64(size_t i) {
const size_t c = i / 1;
const size_t b = c / 8;
const size_t ci = i % 1;
const size_t bc = c % 8;
return 8 * b + 1 * (7 - bc) + (0 - ci);
}

void HELPER(vec_trunc_16_8)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint16_t)); ++i) {
uint16_t aa = *((uint16_t *) a + i);
*((uint8_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_trunc_16_8(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_trunc_16_8(d, a, tcg_constant_i32(size));
}

void HELPER(vec_zext_8_16)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint8_t)); ++i) {
uint8_t aa = *((uint8_t *) a + i);
*((uint16_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_zext_8_16(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_zext_8_16(d, a, tcg_constant_i32(size));
}

void HELPER(vec_sext_8_16)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(int8_t)); ++i) {
int8_t aa = *((int8_t *) a + i);
*((int16_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_sext_8_16(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_sext_8_16(d, a, tcg_constant_i32(size));
}

void HELPER(vec_trunc_32_8)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint32_t)); ++i) {
uint32_t aa = *((uint32_t *) a + i);
*((uint8_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_trunc_32_8(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_trunc_32_8(d, a, tcg_constant_i32(size));
}

void HELPER(vec_zext_8_32)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint8_t)); ++i) {
uint8_t aa = *((uint8_t *) a + i);
*((uint32_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_zext_8_32(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_zext_8_32(d, a, tcg_constant_i32(size));
}

void HELPER(vec_sext_8_32)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(int8_t)); ++i) {
int8_t aa = *((int8_t *) a + i);
*((int32_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_sext_8_32(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_sext_8_32(d, a, tcg_constant_i32(size));
}

void HELPER(vec_trunc_64_8)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint64_t)); ++i) {
uint64_t aa = *((uint64_t *) a + i);
*((uint8_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_trunc_64_8(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_trunc_64_8(d, a, tcg_constant_i32(size));
}

void HELPER(vec_zext_8_64)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint8_t)); ++i) {
uint8_t aa = *((uint8_t *) a + i);
*((uint64_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_zext_8_64(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_zext_8_64(d, a, tcg_constant_i32(size));
}

void HELPER(vec_sext_8_64)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(int8_t)); ++i) {
int8_t aa = *((int8_t *) a + i);
*((int64_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_sext_8_64(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_sext_8_64(d, a, tcg_constant_i32(size));
}

void HELPER(vec_trunc_32_16)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint32_t)); ++i) {
uint32_t aa = *((uint32_t *) a + i);
*((uint16_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_trunc_32_16(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_trunc_32_16(d, a, tcg_constant_i32(size));
}

void HELPER(vec_zext_16_32)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint16_t)); ++i) {
uint16_t aa = *((uint16_t *) a + i);
*((uint32_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_zext_16_32(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_zext_16_32(d, a, tcg_constant_i32(size));
}

void HELPER(vec_sext_16_32)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(int16_t)); ++i) {
int16_t aa = *((int16_t *) a + i);
*((int32_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_sext_16_32(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_sext_16_32(d, a, tcg_constant_i32(size));
}

void HELPER(vec_trunc_64_16)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint64_t)); ++i) {
uint64_t aa = *((uint64_t *) a + i);
*((uint16_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_trunc_64_16(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_trunc_64_16(d, a, tcg_constant_i32(size));
}

void HELPER(vec_zext_16_64)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint16_t)); ++i) {
uint16_t aa = *((uint16_t *) a + i);
*((uint64_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_zext_16_64(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_zext_16_64(d, a, tcg_constant_i32(size));
}

void HELPER(vec_sext_16_64)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(int16_t)); ++i) {
int16_t aa = *((int16_t *) a + i);
*((int64_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_sext_16_64(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_sext_16_64(d, a, tcg_constant_i32(size));
}

void HELPER(vec_trunc_64_32)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint64_t)); ++i) {
uint64_t aa = *((uint64_t *) a + i);
*((uint32_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_trunc_64_32(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_trunc_64_32(d, a, tcg_constant_i32(size));
}

void HELPER(vec_zext_32_64)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(uint32_t)); ++i) {
uint32_t aa = *((uint32_t *) a + i);
*((uint64_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_zext_32_64(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_zext_32_64(d, a, tcg_constant_i32(size));
}

void HELPER(vec_sext_32_64)(void *d, void *a, uint32_t size)
{
for (intptr_t i = 0; i < (size / sizeof(int32_t)); ++i) {
int32_t aa = *((int32_t *) a + i);
*((int64_t *) d + i) = aa;
}
}

static inline void G_GNUC_UNUSED gen_vec_sext_32_64(intptr_t dofs, intptr_t aofs, uint32_t size)
{
TCGv_ptr d = tcg_temp_new_ptr();
TCGv_ptr a = tcg_temp_new_ptr();
tcg_gen_addi_ptr(d, tcg_env, dofs);
tcg_gen_addi_ptr(a, tcg_env, aofs);
gen_helper_vec_sext_32_64(d, a, tcg_constant_i32(size));
}

typedef struct VectorMem {
    uint32_t allocated;
} VectorMem;

static intptr_t temp_new_gvec(VectorMem *mem, uint32_t size)
{
    uint32_t off = ROUND_UP(mem->allocated, size);
    g_assert(off + size <= STRUCT_SIZEOF_FIELD(CPUArchState, tmp_vmem));
    mem->allocated = off + size;
    return offsetof(CPUArchState, tmp_vmem) + off;
}
// void helper_vec_constant
void emit_vec_constant(intptr_t d, intptr_t a) {
VectorMem mem = {0};
intptr_t vec4 = temp_new_gvec(&mem, 32);
static const uint64_t vec4_data[] = {0x191A1B1C1D1E1F20, 0x1112131415161718, 0x90A0B0C0D0E0F10, 0x102030405060708};
tcg_gen_gvec_mov_var(MO_64, tcg_env, vec4, tcg_constant_ptr(vec4_data), 0, 32, 32);
tcg_gen_gvec_add(MO_8, d, a, vec4, 32, 32);
}

// void helper_vec_splat
void emit_vec_splat(intptr_t d, intptr_t a) {
VectorMem mem = {0};
intptr_t vec4 = temp_new_gvec(&mem, 32);
tcg_gen_gvec_dup_imm(MO_64, vec4, 32, 32, 0x102030405060708);
tcg_gen_gvec_add(MO_8, d, a, vec4, 32, 32);
}

// void helper_vec_trunc_uint64_t_uint8_t
void emit_vec_trunc_uint64_t_uint8_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 32);
gen_vec_trunc_64_8(vec6, a, 256);
intptr_t vec5 = temp_new_gvec(&mem, 32);
gen_vec_trunc_64_8(vec5, b, 256);
tcg_gen_gvec_add(MO_8, d, vec5, vec6, 32, 32);
}

// void helper_vec_trunc_uint64_t_uint16_t
void emit_vec_trunc_uint64_t_uint16_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 64);
gen_vec_trunc_64_16(vec6, a, 256);
intptr_t vec5 = temp_new_gvec(&mem, 64);
gen_vec_trunc_64_16(vec5, b, 256);
tcg_gen_gvec_add(MO_16, d, vec5, vec6, 64, 64);
}

// void helper_vec_trunc_uint32_t_uint8_t
void emit_vec_trunc_uint32_t_uint8_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 128);
tcg_gen_gvec_add(MO_32, vec6, b, a, 128, 128);
gen_vec_trunc_32_8(d, vec6, 128);
}

// void helper_vec_trunc_uint32_t_uint16_t
void emit_vec_trunc_uint32_t_uint16_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 128);
tcg_gen_gvec_add(MO_32, vec6, b, a, 128, 128);
gen_vec_trunc_32_16(d, vec6, 128);
}

// void helper_vec_trunc_uint16_t_uint8_t
void emit_vec_trunc_uint16_t_uint8_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 64);
tcg_gen_gvec_add(MO_16, vec6, b, a, 64, 64);
gen_vec_trunc_16_8(d, vec6, 64);
}

// void helper_vec_zext_uint8_t_uint64_t
void emit_vec_zext_uint8_t_uint64_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 256);
gen_vec_zext_8_64(vec6, a, 32);
intptr_t vec5 = temp_new_gvec(&mem, 256);
gen_vec_zext_8_64(vec5, b, 32);
tcg_gen_gvec_add(MO_64, d, vec5, vec6, 256, 256);
}

// void helper_vec_zext_uint16_t_uint64_t
void emit_vec_zext_uint16_t_uint64_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 256);
gen_vec_zext_16_64(vec6, a, 64);
intptr_t vec5 = temp_new_gvec(&mem, 256);
gen_vec_zext_16_64(vec5, b, 64);
tcg_gen_gvec_add(MO_64, d, vec5, vec6, 256, 256);
}

// void helper_vec_zext_uint8_t_uint32_t
void emit_vec_zext_uint8_t_uint32_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 128);
gen_vec_zext_8_32(vec6, a, 32);
intptr_t vec5 = temp_new_gvec(&mem, 128);
gen_vec_zext_8_32(vec5, b, 32);
tcg_gen_gvec_add(MO_32, d, vec5, vec6, 128, 128);
}

// void helper_vec_zext_uint16_t_uint32_t
void emit_vec_zext_uint16_t_uint32_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 128);
gen_vec_zext_16_32(vec6, a, 64);
intptr_t vec5 = temp_new_gvec(&mem, 128);
gen_vec_zext_16_32(vec5, b, 64);
tcg_gen_gvec_add(MO_32, d, vec5, vec6, 128, 128);
}

// void helper_vec_zext_uint8_t_uint16_t
void emit_vec_zext_uint8_t_uint16_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 64);
gen_vec_zext_8_16(vec6, a, 32);
intptr_t vec5 = temp_new_gvec(&mem, 64);
gen_vec_zext_8_16(vec5, b, 32);
tcg_gen_gvec_add(MO_16, d, vec5, vec6, 64, 64);
}

// void helper_vec_sext_int8_t_int64_t
void emit_vec_sext_int8_t_int64_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 256);
gen_vec_sext_8_64(vec6, a, 32);
intptr_t vec5 = temp_new_gvec(&mem, 256);
gen_vec_sext_8_64(vec5, b, 32);
tcg_gen_gvec_add(MO_64, d, vec5, vec6, 256, 256);
}

// void helper_vec_sext_int16_t_int64_t
void emit_vec_sext_int16_t_int64_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 256);
gen_vec_sext_16_64(vec6, a, 64);
intptr_t vec5 = temp_new_gvec(&mem, 256);
gen_vec_sext_16_64(vec5, b, 64);
tcg_gen_gvec_add(MO_64, d, vec5, vec6, 256, 256);
}

// void helper_vec_sext_int8_t_int32_t
void emit_vec_sext_int8_t_int32_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 128);
gen_vec_sext_8_32(vec6, a, 32);
intptr_t vec5 = temp_new_gvec(&mem, 128);
gen_vec_sext_8_32(vec5, b, 32);
tcg_gen_gvec_add(MO_32, d, vec5, vec6, 128, 128);
}

// void helper_vec_sext_int16_t_int32_t
void emit_vec_sext_int16_t_int32_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 128);
gen_vec_sext_16_32(vec6, a, 64);
intptr_t vec5 = temp_new_gvec(&mem, 128);
gen_vec_sext_16_32(vec5, b, 64);
tcg_gen_gvec_add(MO_32, d, vec5, vec6, 128, 128);
}

// void helper_vec_sext_int8_t_int16_t
void emit_vec_sext_int8_t_int16_t(intptr_t d, intptr_t a, intptr_t b) {
VectorMem mem = {0};
intptr_t vec6 = temp_new_gvec(&mem, 64);
gen_vec_sext_8_16(vec6, a, 32);
intptr_t vec5 = temp_new_gvec(&mem, 64);
gen_vec_sext_8_16(vec5, b, 32);
tcg_gen_gvec_add(MO_16, d, vec5, vec6, 64, 64);
}

