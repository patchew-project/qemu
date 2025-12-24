/*
 * RISC-V GDB Server Stub
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "exec/gdbstub.h"
#include "gdbstub/helpers.h"
#include "cpu.h"

/*
 * Vector lane type definitions for GDB target description.
 * Uses GDB's builtin type names (uint8, int8, ieee_half, etc.)
 */
enum RVVExtension {
    RVV_EXT_NONE = 0,   /* Zve32x (checked at call site) */
    RVV_EXT_ZVE64X,     /* 64-bit integer elements */
    RVV_EXT_ZVE64D,     /* Double-precision float */
    RVV_EXT_ZVE32F,     /* Single-precision float */
    RVV_EXT_ZVFHMIN,    /* Half-precision float */
    RVV_EXT_ZVFBFMIN,   /* BFloat16 */
};

struct TypeSize {
    const char *gdb_type;        /* GDB builtin type name */
    const char *name;            /* Short name for union field (NULL = use gdb_type) */
    int size;                    /* Element size in bits */
    enum RVVExtension required;  /* Required extension, RVV_EXT_NONE if always enabled */
};

int riscv_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    target_ulong tmp;

    if (n < 32) {
        tmp = env->gpr[n];
    } else if (n == 32) {
        tmp = env->pc;
    } else {
        return 0;
    }

    switch (mcc->def->misa_mxl_max) {
    case MXL_RV32:
        return gdb_get_reg32(mem_buf, tmp);
    case MXL_RV64:
    case MXL_RV128:
        return gdb_get_reg64(mem_buf, tmp);
    default:
        g_assert_not_reached();
    }
    return 0;
}

int riscv_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    int length = 0;
    target_ulong tmp;

    switch (mcc->def->misa_mxl_max) {
    case MXL_RV32:
        tmp = (int32_t)ldl_p(mem_buf);
        length = 4;
        break;
    case MXL_RV64:
    case MXL_RV128:
        if (env->xl < MXL_RV64) {
            tmp = (int32_t)ldq_p(mem_buf);
        } else {
            tmp = ldq_p(mem_buf);
        }
        length = 8;
        break;
    default:
        g_assert_not_reached();
    }
    if (n > 0 && n < 32) {
        env->gpr[n] = tmp;
    } else if (n == 32) {
        env->pc = tmp;
    }

    return length;
}

static int riscv_gdb_get_fpu(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < 32) {
        if (env->misa_ext & RVD) {
            return gdb_get_reg64(buf, env->fpr[n]);
        }
        if (env->misa_ext & RVF) {
            return gdb_get_reg32(buf, env->fpr[n]);
        }
    }
    return 0;
}

static int riscv_gdb_set_fpu(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < 32) {
        env->fpr[n] = ldq_p(mem_buf); /* always 64-bit */
        return sizeof(uint64_t);
    }
    return 0;
}

static int riscv_gdb_get_vector(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    uint16_t vlenb = cpu->cfg.vlenb;
    if (n < 32) {
        int i;
        int cnt = 0;
        for (i = 0; i < vlenb; i += 8) {
            cnt += gdb_get_reg64(buf,
                                 env->vreg[(n * vlenb + i) / 8]);
        }
        return cnt;
    }

    return 0;
}

static int riscv_gdb_set_vector(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    uint16_t vlenb = cpu->cfg.vlenb;
    if (n < 32) {
        int i;
        for (i = 0; i < vlenb; i += 8) {
            env->vreg[(n * vlenb + i) / 8] = ldq_p(mem_buf + i);
        }
        return vlenb;
    }

    return 0;
}

static int riscv_gdb_get_csr(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < CSR_TABLE_SIZE) {
        target_ulong val = 0;
        int result;

        result = riscv_csrrw_debug(env, n, &val, 0, 0);
        if (result == RISCV_EXCP_NONE) {
            return gdb_get_regl(buf, val);
        }
    }
    return 0;
}

static int riscv_gdb_set_csr(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < CSR_TABLE_SIZE) {
        target_ulong val = ldtul_p(mem_buf);
        int result;

        result = riscv_csrrw_debug(env, n, NULL, val, -1);
        if (result == RISCV_EXCP_NONE) {
            return sizeof(target_ulong);
        }
    }
    return 0;
}

static int riscv_gdb_get_virtual(CPUState *cs, GByteArray *buf, int n)
{
    if (n == 0) {
#ifdef CONFIG_USER_ONLY
        return gdb_get_regl(buf, 0);
#else
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;

        /* Per RiscV debug spec v1.0.0 rc4 */
        target_ulong vbit = (env->virt_enabled) ? BIT(2) : 0;

        return gdb_get_regl(buf, env->priv | vbit);
#endif
    }
    return 0;
}

static int riscv_gdb_set_virtual(CPUState *cs, uint8_t *mem_buf, int n)
{
    if (n == 0) {
#ifndef CONFIG_USER_ONLY
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;

        target_ulong new_priv = ldtul_p(mem_buf) & 0x3;
        bool new_virt = 0;

        if (new_priv == PRV_RESERVED) {
            new_priv = PRV_S;
        }

        if (new_priv != PRV_M) {
            new_virt = (ldtul_p(mem_buf) & BIT(2)) >> 2;
        }

        if (riscv_has_ext(env, RVH) && new_virt != env->virt_enabled) {
            riscv_cpu_swap_hypervisor_regs(env);
        }

        riscv_cpu_set_mode(env, new_priv, new_virt);
#endif
        return sizeof(target_ulong);
    }
    return 0;
}

static GDBFeature *riscv_gen_dynamic_csr_feature(CPUState *cs, int base_reg)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    GDBFeatureBuilder builder;
    riscv_csr_predicate_fn predicate;
    int bitsize = riscv_cpu_max_xlen(mcc);
    const char *name;
    int i;

#if !defined(CONFIG_USER_ONLY)
    env->debugger = true;
#endif

    /* Until gdb knows about 128-bit registers */
    if (bitsize > 64) {
        bitsize = 64;
    }

    gdb_feature_builder_init(&builder, &cpu->dyn_csr_feature,
                             "org.gnu.gdb.riscv.csr", "riscv-csr.xml",
                             base_reg);

    for (i = 0; i < CSR_TABLE_SIZE; i++) {
        if (env->priv_ver < csr_ops[i].min_priv_ver) {
            continue;
        }
        predicate = csr_ops[i].predicate;
        if (predicate && (predicate(env, i) == RISCV_EXCP_NONE)) {
            name = csr_ops[i].name;
            if (!name) {
                name = g_strdup_printf("csr%03x", i);
            }

            gdb_feature_builder_append_reg(&builder, name, bitsize, i,
                                           "int", NULL);
        }
    }

    gdb_feature_builder_end(&builder);

#if !defined(CONFIG_USER_ONLY)
    env->debugger = false;
#endif

    return &cpu->dyn_csr_feature;
}

/*
 * Vector lane types using GDB's builtin type names.
 * Float types are conditionally included based on extension availability.
 */
static const struct TypeSize vec_lanes[] = {
    /* 128 bit - requires Zve64x */
    { "uint128",     NULL,   128, RVV_EXT_ZVE64X },
    { "int128",      NULL,   128, RVV_EXT_ZVE64X },
    /* 64 bit - requires Zve64x */
    { "uint64",      NULL,   64,  RVV_EXT_ZVE64X },
    { "int64",       NULL,   64,  RVV_EXT_ZVE64X },
    { "ieee_double", "fp64", 64,  RVV_EXT_ZVE64D },
    /* 32 bit */
    { "uint32",      NULL,   32,  RVV_EXT_NONE },
    { "int32",       NULL,   32,  RVV_EXT_NONE },
    { "ieee_single", "fp32", 32,  RVV_EXT_ZVE32F },
    /* 16 bit */
    { "uint16",      NULL,   16,  RVV_EXT_NONE },
    { "int16",       NULL,   16,  RVV_EXT_NONE },
    { "ieee_half",   "fp16", 16,  RVV_EXT_ZVFHMIN },
    { "bfloat16",    "bf16", 16,  RVV_EXT_ZVFBFMIN },
    /* 8 bit */
    { "uint8",       NULL,   8,   RVV_EXT_NONE },
    { "int8",        NULL,   8,   RVV_EXT_NONE },
};

/* Check if a vector lane type should be included based on CPU extensions */
static bool riscv_gdb_vec_lane_enabled(RISCVCPU *cpu, const struct TypeSize *ts)
{
    switch (ts->required) {
    case RVV_EXT_NONE:
        return true;
    case RVV_EXT_ZVE64X:
        return cpu->cfg.ext_zve64x;
    case RVV_EXT_ZVE64D:
        return cpu->cfg.ext_zve64d;
    case RVV_EXT_ZVE32F:
        return cpu->cfg.ext_zve32f;
    case RVV_EXT_ZVFHMIN:
        return cpu->cfg.ext_zvfhmin;
    case RVV_EXT_ZVFBFMIN:
        return cpu->cfg.ext_zvfbfmin;
    default:
        return false;
    }
}

static GDBFeature *ricsv_gen_dynamic_vector_feature(CPUState *cs, int base_reg)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    int bitsize = cpu->cfg.vlenb << 3;
    GDBFeatureBuilder builder;
    int i;

    gdb_feature_builder_init(&builder, &cpu->dyn_vreg_feature,
                             "org.gnu.gdb.riscv.vector", "riscv-vector.xml",
                             base_reg);

    /* Define vector types for each lane type */
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        const struct TypeSize *ts = &vec_lanes[i];
        if (!riscv_gdb_vec_lane_enabled(cpu, ts)) {
            continue;
        }
        gdb_feature_builder_append_tag(
            &builder, "<vector id=\"%s\" type=\"%s\" count=\"%d\"/>",
            ts->gdb_type, ts->gdb_type, bitsize / ts->size);
    }

    /* Create a single flat union with all type views */
    gdb_feature_builder_append_tag(&builder, "<union id=\"riscv_vector\">");
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        const struct TypeSize *ts = &vec_lanes[i];
        const char *name = ts->name ? ts->name : ts->gdb_type;
        if (!riscv_gdb_vec_lane_enabled(cpu, ts)) {
            continue;
        }
        gdb_feature_builder_append_tag(&builder,
                                       "<field name=\"%s\" type=\"%s\"/>",
                                       name, ts->gdb_type);
    }

    /* Add backward-compatible aliases for unsigned types */
    gdb_feature_builder_append_tag(&builder,
                                   "<field name=\"b\" type=\"uint8\"/>");
    gdb_feature_builder_append_tag(&builder,
                                   "<field name=\"s\" type=\"uint16\"/>");
    gdb_feature_builder_append_tag(&builder,
                                   "<field name=\"w\" type=\"uint32\"/>");
    if (cpu->cfg.ext_zve64x) {
        gdb_feature_builder_append_tag(&builder,
                                       "<field name=\"l\" type=\"uint64\"/>");
        gdb_feature_builder_append_tag(&builder,
                                       "<field name=\"q\" type=\"uint128\"/>");
    }

    gdb_feature_builder_append_tag(&builder, "</union>");

    /* Define vector registers */
    for (i = 0; i < 32; i++) {
        gdb_feature_builder_append_reg(&builder, g_strdup_printf("v%d", i),
                                       bitsize, i, "riscv_vector", "vector");
    }

    gdb_feature_builder_end(&builder);

    return &cpu->dyn_vreg_feature;
}

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    if (env->misa_ext & RVD) {
        gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                                 gdb_find_static_feature("riscv-64bit-fpu.xml"),
                                 0);
    } else if (env->misa_ext & RVF) {
        gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                                 gdb_find_static_feature("riscv-32bit-fpu.xml"),
                                 0);
    }
    if (cpu->cfg.ext_zve32x) {
        gdb_register_coprocessor(cs, riscv_gdb_get_vector,
                                 riscv_gdb_set_vector,
                                 ricsv_gen_dynamic_vector_feature(cs, cs->gdb_num_regs),
                                 0);
    }
    switch (mcc->def->misa_mxl_max) {
    case MXL_RV32:
        gdb_register_coprocessor(cs, riscv_gdb_get_virtual,
                                 riscv_gdb_set_virtual,
                                 gdb_find_static_feature("riscv-32bit-virtual.xml"),
                                 0);
        break;
    case MXL_RV64:
    case MXL_RV128:
        gdb_register_coprocessor(cs, riscv_gdb_get_virtual,
                                 riscv_gdb_set_virtual,
                                 gdb_find_static_feature("riscv-64bit-virtual.xml"),
                                 0);
        break;
    default:
        g_assert_not_reached();
    }

    if (cpu->cfg.ext_zicsr) {
        gdb_register_coprocessor(cs, riscv_gdb_get_csr, riscv_gdb_set_csr,
                                 riscv_gen_dynamic_csr_feature(cs, cs->gdb_num_regs),
                                 0);
    }
}
