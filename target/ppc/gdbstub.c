/*
 * PowerPC gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "gdbstub/registers.h"
#include "internal.h"

static int ppc_gdb_register_len_apple(int n)
{
    switch (n) {
    case 0 ... 31:
        /* gprs */
        return 8;
    case 32 ... 63:
        /* fprs */
        return 8;
    case 64 ... 95:
        return 16;
    case 64 + 32: /* nip */
    case 65 + 32: /* msr */
    case 67 + 32: /* lr */
    case 68 + 32: /* ctr */
    case 70 + 32: /* fpscr */
        return 8;
    case 66 + 32: /* cr */
    case 69 + 32: /* xer */
        return 4;
    default:
        return 0;
    }
}

static int ppc_gdb_register_len(int n)
{
    switch (n) {
    case 0 ... 31:
        /* gprs */
        return sizeof(target_ulong);
    case 66:
        /* cr */
    case 69:
        /* xer */
        return 4;
    case 64:
        /* nip */
    case 65:
        /* msr */
    case 67:
        /* lr */
    case 68:
        /* ctr */
        return sizeof(target_ulong);
    default:
        return 0;
    }
}

/*
 * We need to map the target endian registers from gdb in the
 * "current" memory ordering. For user-only mode we get this for free;
 * TARGET_BIG_ENDIAN is set to the proper ordering for the binary, and
 * cannot be changed. For system mode, TARGET_BIG_ENDIAN is always
 * set, and we must check the current mode of the chip to see if we're
 * running in little-endian.
 */
static void ppc_maybe_bswap_register(CPUPPCState *env, uint8_t *mem_buf, int len)
{
#ifndef CONFIG_USER_ONLY
    if (!FIELD_EX64(env->msr, MSR, LE)) {
        /* do nothing */
    } else if (len == 4) {
        bswap32s((uint32_t *)mem_buf);
    } else if (len == 8) {
        bswap64s((uint64_t *)mem_buf);
    } else if (len == 16) {
        bswap128s((Int128 *)mem_buf);
    } else {
        g_assert_not_reached();
    }
#endif
}

/*
 * We need to present the registers to gdb in the "current" memory
 * ordering. For user-only mode this is hardwired by TARGET_BIG_ENDIAN
 * and cannot be changed. For system mode we must check the current
 * mode of the chip to see if we're running in little-endian.
 */
static MemOp ppc_gdb_memop(CPUPPCState *env, int len)
{
#ifndef CONFIG_USER_ONLY
    MemOp end = FIELD_EX64(env->msr, MSR, LE) ? MO_LE : MO_BE;
#else
    #ifdef TARGET_BIG_ENDIAN
    MemOp end = MO_BE;
    #else
    MemOp end = MO_LE;
    #endif
#endif

    return size_memop(len) | end;
}

/*
 * Helpers copied from helpers.h just for loading target_ulong values
 * from gdbstub's GByteArray
 */

#if TARGET_LONG_BITS == 64
#define ldtul_p(addr) ldq_p(addr)
#else
#define ldtul_p(addr) ldl_p(addr)
#endif

/*
 * Old gdb always expects FP registers.  Newer (xml-aware) gdb only
 * expects whatever the target description contains.  Due to a
 * historical mishap the FP registers appear in between core integer
 * regs and PC, MSR, CR, and so forth.  We hack round this by giving
 * the FP regs zero size when talking to a newer gdb.
 */

int ppc_cpu_gdb_read_register(CPUState *cs, GByteArray *buf, int n)
{
    CPUPPCState *env = cpu_env(cs);
    int r = ppc_gdb_register_len(n);
    MemOp mo;

    if (!r) {
        return r;
    }

    mo = ppc_gdb_memop(env, r);

    if (n < 32) {
        /* gprs */
        return gdb_get_register_value(mo, buf, (uint8_t *) &env->gpr[n]);
    } else {
        switch (n) {
        case 64:
            return gdb_get_register_value(mo, buf, (uint8_t *) &env->nip);
        case 65:
            return gdb_get_register_value(mo, buf, (uint8_t *) &env->msr);
        case 66:
            {
                uint32_t cr = ppc_get_cr(env);
                return gdb_get_register_value(ppc_gdb_memop(env, 4), buf, (uint8_t *) &cr);
            }
        case 67:
            return gdb_get_register_value(mo, buf, (uint8_t *) &env->lr);
            break;
        case 68:
            return gdb_get_register_value(mo, buf, (uint8_t *) &env->ctr);
            break;
        case 69:
            uint32_t val = cpu_read_xer(env);
            return gdb_get_register_value(ppc_gdb_memop(env, 4), buf, (uint8_t *) &val);
        }
    }

    return 0;
}

int ppc_cpu_gdb_read_register_apple(CPUState *cs, GByteArray *buf, int n)
{
    CPUPPCState *env = cpu_env(cs);
    int r = ppc_gdb_register_len_apple(n);
    MemOp mo = ppc_gdb_memop(env, r);
    int actual = 0;

    if (!r) {
        return r;
    }

    if (n < 32) {
        /* gprs */
        actual = gdb_get_register_value(mo, buf, (uint8_t *) &env->gpr[n]);
    } else if (n < 64) {
        /* fprs */
        actual = gdb_get_register_value(mo, buf, (uint8_t *) cpu_fpr_ptr(env, n - 32));
    } else if (n < 96) {
        /* Altivec - where are they? ppc_vsr_t vsr[64]? */
        uint64_t empty = 0;
        actual = gdb_get_register_value(mo, buf, (uint8_t *) &empty);
        actual = gdb_get_register_value(mo, buf, (uint8_t *) &empty);
    } else {
        switch (n) {
        case 64 + 32:
            actual = gdb_get_register_value(mo, buf, (uint8_t *) &env->nip);
            break;
        case 65 + 32:
            actual = gdb_get_register_value(mo, buf, (uint8_t *) &env->msr);
            break;
        case 66 + 32:
        {
            uint32_t cr = ppc_get_cr(env);
            actual = gdb_get_register_value(mo, buf, (uint8_t *) &cr);
            break;
        }
        case 67 + 32:
            actual = gdb_get_register_value(mo, buf, (uint8_t *) &env->lr);
            break;
        case 68 + 32:
            actual = gdb_get_register_value(mo, buf, (uint8_t *) &env->ctr);
            break;
        case 69 + 32:
        {
            uint32_t xer = cpu_read_xer(env);
            actual = gdb_get_register_value(mo, buf, (uint8_t *) &xer);
            break;
        }
        case 70 + 32:
            actual = gdb_get_register_value(mo, buf, (uint8_t *) &env->fpscr);
            break;
        }
    }

    g_assert(r == actual);
    return r;
}

int ppc_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPUPPCState *env = cpu_env(cs);
    int r = ppc_gdb_register_len(n);

    if (!r) {
        return r;
    }

    g_assert(r == n);
    
    ppc_maybe_bswap_register(env, mem_buf, r);
    if (n < 32) {
        /* gprs */
        env->gpr[n] = ldtul_p(mem_buf);
    } else if (n < 64) {
        /* fprs */
        *cpu_fpr_ptr(env, n - 32) = ldq_p(mem_buf);
    } else {
        switch (n) {
        case 64:
            env->nip = ldtul_p(mem_buf);
            break;
        case 65:
            ppc_store_msr(env, ldtul_p(mem_buf));
            break;
        case 66:
            {
                uint32_t cr = ldl_p(mem_buf);
                ppc_set_cr(env, cr);
                break;
            }
        case 67:
            env->lr = ldtul_p(mem_buf);
            break;
        case 68:
            env->ctr = ldtul_p(mem_buf);
            break;
        case 69:
            cpu_write_xer(env, ldl_p(mem_buf));
            break;
        case 70:
            /* fpscr */
            ppc_store_fpscr(env, ldtul_p(mem_buf));
            break;
        }
    }
    return r;
}
int ppc_cpu_gdb_write_register_apple(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPUPPCState *env = cpu_env(cs);
    int r = ppc_gdb_register_len_apple(n);

    if (!r) {
        return r;
    }
    ppc_maybe_bswap_register(env, mem_buf, r);
    if (n < 32) {
        /* gprs */
        env->gpr[n] = ldq_p(mem_buf);
    } else if (n < 64) {
        /* fprs */
        *cpu_fpr_ptr(env, n - 32) = ldq_p(mem_buf);
    } else {
        switch (n) {
        case 64 + 32:
            env->nip = ldq_p(mem_buf);
            break;
        case 65 + 32:
            ppc_store_msr(env, ldq_p(mem_buf));
            break;
        case 66 + 32:
            {
                uint32_t cr = ldl_p(mem_buf);
                ppc_set_cr(env, cr);
                break;
            }
        case 67 + 32:
            env->lr = ldq_p(mem_buf);
            break;
        case 68 + 32:
            env->ctr = ldq_p(mem_buf);
            break;
        case 69 + 32:
            cpu_write_xer(env, ldl_p(mem_buf));
            break;
        case 70 + 32:
            /* fpscr */
            ppc_store_fpscr(env, ldq_p(mem_buf));
            break;
        }
    }
    return r;
}

#ifndef CONFIG_USER_ONLY
static void gdb_gen_spr_feature(CPUState *cs)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    GDBFeatureBuilder builder;
    unsigned int num_regs = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(env->spr_cb); i++) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (!spr->name) {
            continue;
        }

        /*
         * GDB identifies registers based on the order they are
         * presented in the XML. These ids will not match QEMU's
         * representation (which follows the PowerISA).
         *
         * Store the position of the current register description so
         * we can make the correspondence later.
         */
        spr->gdb_id = num_regs;
        num_regs++;
    }

    if (pcc->gdb_spr.xml) {
        return;
    }

    gdb_feature_builder_init(&builder, &pcc->gdb_spr,
                             "org.qemu.power.spr", "power-spr.xml",
                             cs->gdb_num_regs);

    for (i = 0; i < ARRAY_SIZE(env->spr_cb); i++) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (!spr->name) {
            continue;
        }

        gdb_feature_builder_append_reg(&builder, g_ascii_strdown(spr->name, -1),
                                       TARGET_LONG_BITS, spr->gdb_id,
                                       "int", "spr");
    }

    gdb_feature_builder_end(&builder);
}
#endif

#if !defined(CONFIG_USER_ONLY)
static int gdb_find_spr_idx(CPUPPCState *env, int n)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(env->spr_cb); i++) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (spr->name && spr->gdb_id == n) {
            return i;
        }
    }
    return -1;
}

static int gdb_get_spr_reg(CPUState *cs, GByteArray *buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    MemOp mo = ppc_gdb_memop(env, TARGET_LONG_SIZE);
    target_ulong val;
    int reg;

    reg = gdb_find_spr_idx(env, n);
    if (reg < 0) {
        return 0;
    }

    /* Handle those SPRs that are not part of the env->spr[] array */
    switch (reg) {
#if defined(TARGET_PPC64)
    case SPR_CFAR:
        val = env->cfar;
        break;
#endif
    case SPR_HDEC:
        val = cpu_ppc_load_hdecr(env);
        break;
    case SPR_TBL:
        val = cpu_ppc_load_tbl(env);
        break;
    case SPR_TBU:
        val = cpu_ppc_load_tbu(env);
        break;
    case SPR_DECR:
        val = cpu_ppc_load_decr(env);
        break;
    default:
        val = env->spr[reg];
    }
    return gdb_get_register_value(mo, buf, (uint8_t *) &val);
}

static int gdb_set_spr_reg(CPUState *cs, uint8_t *mem_buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    int reg;
    int len;

    reg = gdb_find_spr_idx(env, n);
    if (reg < 0) {
        return 0;
    }

    len = TARGET_LONG_SIZE;
    ppc_maybe_bswap_register(env, mem_buf, len);

    /* Handle those SPRs that are not part of the env->spr[] array */
    target_ulong val = ldn_p(mem_buf, len);
    switch (reg) {
#if defined(TARGET_PPC64)
    case SPR_CFAR:
        env->cfar = val;
        break;
#endif
    default:
        env->spr[reg] = val;
    }

    return len;
}
#endif

static int gdb_get_float_reg(CPUState *cs, GByteArray *buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    MemOp mo;
    if (n < 32) {
        mo = ppc_gdb_memop(env, 8);
        return gdb_get_register_value(mo, buf, (uint8_t *)cpu_fpr_ptr(env, n));
    }
    if (n == 32) {
        mo = ppc_gdb_memop(env, 4);
        return gdb_get_register_value(mo, buf, (uint8_t *) &env->fpscr);
    }
    return 0;
}

static int gdb_set_float_reg(CPUState *cs, uint8_t *mem_buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    if (n < 32) {
        ppc_maybe_bswap_register(env, mem_buf, 8);
        *cpu_fpr_ptr(env, n) = ldq_p(mem_buf);
        return 8;
    }
    if (n == 32) {
        ppc_maybe_bswap_register(env, mem_buf, 4);
        ppc_store_fpscr(env, ldl_p(mem_buf));
        return 4;
    }
    return 0;
}

static int gdb_get_avr_reg(CPUState *cs, GByteArray *buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    MemOp mo;

    if (n < 32) {
        ppc_avr_t *avr = cpu_avr_ptr(env, n);
        mo = ppc_gdb_memop(env, 16);
        return gdb_get_register_value(mo, buf, (uint8_t *) avr);
    }
    if (n == 32) {
        uint32_t vscr = ppc_get_vscr(env);
        mo = ppc_gdb_memop(env, 4);
        return gdb_get_register_value(mo, buf, (uint8_t *) &vscr);
    }
    if (n == 33) {
        mo = ppc_gdb_memop(env, 4);
        return gdb_get_register_value(mo, buf, (uint8_t *) &env->spr[SPR_VRSAVE]);
    }
    return 0;
}

static int gdb_set_avr_reg(CPUState *cs, uint8_t *mem_buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    if (n < 32) {
        ppc_avr_t *avr = cpu_avr_ptr(env, n);
        ppc_maybe_bswap_register(env, mem_buf, 16);
        avr->VsrD(0) = ldq_p(mem_buf);
        avr->VsrD(1) = ldq_p(mem_buf + 8);
        return 16;
    }
    if (n == 32) {
        ppc_maybe_bswap_register(env, mem_buf, 4);
        ppc_store_vscr(env, ldl_p(mem_buf));
        return 4;
    }
    if (n == 33) {
        ppc_maybe_bswap_register(env, mem_buf, 4);
        env->spr[SPR_VRSAVE] = (target_ulong)ldl_p(mem_buf);
        return 4;
    }
    return 0;
}

static int gdb_get_spe_reg(CPUState *cs, GByteArray *buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    MemOp mo;

    if (n < 32) {
#if defined(TARGET_PPC64)
        uint32_t low = env->gpr[n] >> 32;
        mo = ppc_gdb_memop(env, 4);
        return gdb_get_register_value(mo, buf, (uint8_t *) &low);
#else
        mo = ppc_gdb_memop(env, 4);
        return gdb_get_register_value(mo, buf, (uint8_t *) &env->gprh[n]);
#endif
    }
    if (n == 32) {
        mo = ppc_gdb_memop(env, 8);
        return gdb_get_register_value(mo, buf, (uint8_t *) &env->spe_acc);
    }
    if (n == 33) {
        mo = ppc_gdb_memop(env, 4);
        return gdb_get_register_value(mo, buf, (uint8_t *) &env->spe_fscr);
    }
    return 0;
}

static int gdb_set_spe_reg(CPUState *cs, uint8_t *mem_buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    if (n < 32) {
#if defined(TARGET_PPC64)
        target_ulong lo = (uint32_t)env->gpr[n];
        target_ulong hi;

        ppc_maybe_bswap_register(env, mem_buf, 4);

        hi = (target_ulong)ldl_p(mem_buf) << 32;
        env->gpr[n] = lo | hi;
#else
        env->gprh[n] = ldl_p(mem_buf);
#endif
        return 4;
    }
    if (n == 32) {
        ppc_maybe_bswap_register(env, mem_buf, 8);
        env->spe_acc = ldq_p(mem_buf);
        return 8;
    }
    if (n == 33) {
        ppc_maybe_bswap_register(env, mem_buf, 4);
        env->spe_fscr = ldl_p(mem_buf);
        return 4;
    }
    return 0;
}

static int gdb_get_vsx_reg(CPUState *cs, GByteArray *buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    if (n < 32) {
        return gdb_get_register_value(ppc_gdb_memop(env, 8),
                                      buf,
                                      (uint8_t *)cpu_vsrl_ptr(env, n));
    }
    return 0;
}

static int gdb_set_vsx_reg(CPUState *cs, uint8_t *mem_buf, int n)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    if (n < 32) {
        ppc_maybe_bswap_register(env, mem_buf, 8);
        *cpu_vsrl_ptr(env, n) = ldq_p(mem_buf);
        return 8;
    }
    return 0;
}

const gchar *ppc_gdb_arch_name(CPUState *cs)
{
#if defined(TARGET_PPC64)
    return "powerpc:common64";
#else
    return "powerpc:common";
#endif
}

void ppc_gdb_init(CPUState *cs, PowerPCCPUClass *pcc)
{
    if (pcc->insns_flags & PPC_FLOAT) {
        gdb_register_coprocessor(cs, gdb_get_float_reg, gdb_set_float_reg,
                                 gdb_find_static_feature("power-fpu.xml"), 0);
    }
    if (pcc->insns_flags & PPC_ALTIVEC) {
        gdb_register_coprocessor(cs, gdb_get_avr_reg, gdb_set_avr_reg,
                                 gdb_find_static_feature("power-altivec.xml"),
                                 0);
    }
    if (pcc->insns_flags & PPC_SPE) {
        gdb_register_coprocessor(cs, gdb_get_spe_reg, gdb_set_spe_reg,
                                 gdb_find_static_feature("power-spe.xml"), 0);
    }
    if (pcc->insns_flags2 & PPC2_VSX) {
        gdb_register_coprocessor(cs, gdb_get_vsx_reg, gdb_set_vsx_reg,
                                 gdb_find_static_feature("power-vsx.xml"), 0);
    }
#ifndef CONFIG_USER_ONLY
    gdb_gen_spr_feature(cs);
    gdb_register_coprocessor(cs, gdb_get_spr_reg, gdb_set_spr_reg,
                             &pcc->gdb_spr, 0);
#endif
}
