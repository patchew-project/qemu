/*
 * ARM gdb server stub
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
#include "internals.h"
#include "cpregs.h"

typedef struct RegisterSysregXmlParam {
    CPUState *cs;
    GString *s;
    int n;
} RegisterSysregXmlParam;

/* Old gdb always expect FPA registers.  Newer (xml-aware) gdb only expect
   whatever the target description contains.  Due to a historical mishap
   the FPA registers appear in between core integer regs and the CPSR.
   We hack round this by giving the FPA regs zero size when talking to a
   newer gdb.  */

int arm_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (n < 16) {
        /* Core integer register.  */
        return gdb_get_reg32(mem_buf, env->regs[n]);
    }
    if (n < 24) {
        /* FPA registers.  */
        if (gdb_has_xml) {
            return 0;
        }
        return gdb_get_zeroes(mem_buf, 12);
    }
    switch (n) {
    case 24:
        /* FPA status register.  */
        if (gdb_has_xml) {
            return 0;
        }
        return gdb_get_reg32(mem_buf, 0);
    case 25:
        /* CPSR, or XPSR for M-profile */
        if (arm_feature(env, ARM_FEATURE_M)) {
            return gdb_get_reg32(mem_buf, xpsr_read(env));
        } else {
            return gdb_get_reg32(mem_buf, cpsr_read(env));
        }
    }
    /* Unknown register.  */
    return 0;
}

int arm_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    /*
     * Mask out low bits of PC to workaround gdb bugs.
     * This avoids an assert in thumb_tr_translate_insn, because it is
     * architecturally impossible to misalign the pc.
     * This will probably cause problems if we ever implement the
     * Jazelle DBX extensions.
     */
    if (n == 15) {
        tmp &= ~1;
    }

    if (n < 16) {
        /* Core integer register.  */
        if (n == 13 && arm_feature(env, ARM_FEATURE_M)) {
            /* M profile SP low bits are always 0 */
            tmp &= ~3;
        }
        env->regs[n] = tmp;
        return 4;
    }
    if (n < 24) { /* 16-23 */
        /* FPA registers (ignored).  */
        if (gdb_has_xml) {
            return 0;
        }
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register (ignored).  */
        if (gdb_has_xml) {
            return 0;
        }
        return 4;
    case 25:
        /* CPSR, or XPSR for M-profile */
        if (arm_feature(env, ARM_FEATURE_M)) {
            /*
             * Don't allow writing to XPSR.Exception as it can cause
             * a transition into or out of handler mode (it's not
             * writable via the MSR insn so this is a reasonable
             * restriction). Other fields are safe to update.
             */
            xpsr_write(env, tmp, ~XPSR_EXCP);
        } else {
            cpsr_write(env, tmp, 0xffffffff, CPSRWriteByGDBStub);
        }
        return 4;
    }
    /* Unknown register.  */
    return 0;
}

static int vfp_gdb_get_reg(CPUARMState *env, GByteArray *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);
    int nregs = cpu_isar_feature(aa32_simd_r32, cpu) ? 32 : 16;

    /* VFP data registers are always little-endian.  */
    if (reg < nregs) {
        return gdb_get_reg64(buf, *aa32_vfp_dreg(env, reg));
    }
    if (arm_feature(env, ARM_FEATURE_NEON)) {
        /* Aliases for Q regs.  */
        nregs += 16;
        if (reg < nregs) {
            uint64_t *q = aa32_vfp_qreg(env, reg - 32);
            return gdb_get_reg128(buf, q[0], q[1]);
        }
    }
    switch (reg - nregs) {
    case 0:
        return gdb_get_reg32(buf, vfp_get_fpscr(env));
    }
    return 0;
}

static int vfp_gdb_set_reg(CPUARMState *env, uint8_t *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);
    int nregs = cpu_isar_feature(aa32_simd_r32, cpu) ? 32 : 16;

    if (reg < nregs) {
        *aa32_vfp_dreg(env, reg) = ldq_le_p(buf);
        return 8;
    }
    if (arm_feature(env, ARM_FEATURE_NEON)) {
        nregs += 16;
        if (reg < nregs) {
            uint64_t *q = aa32_vfp_qreg(env, reg - 32);
            q[0] = ldq_le_p(buf);
            q[1] = ldq_le_p(buf + 8);
            return 16;
        }
    }
    switch (reg - nregs) {
    case 0:
        vfp_set_fpscr(env, ldl_p(buf));
        return 4;
    }
    return 0;
}

static int vfp_gdb_get_sysreg(CPUARMState *env, GByteArray *buf, int reg)
{
    switch (reg) {
    case 0:
        return gdb_get_reg32(buf, env->vfp.xregs[ARM_VFP_FPSID]);
    case 1:
        return gdb_get_reg32(buf, env->vfp.xregs[ARM_VFP_FPEXC]);
    }
    return 0;
}

static int vfp_gdb_set_sysreg(CPUARMState *env, uint8_t *buf, int reg)
{
    switch (reg) {
    case 0:
        env->vfp.xregs[ARM_VFP_FPSID] = ldl_p(buf);
        return 4;
    case 1:
        env->vfp.xregs[ARM_VFP_FPEXC] = ldl_p(buf) & (1 << 30);
        return 4;
    }
    return 0;
}

static int mve_gdb_get_reg(CPUARMState *env, GByteArray *buf, int reg)
{
    switch (reg) {
    case 0:
        return gdb_get_reg32(buf, env->v7m.vpr);
    default:
        return 0;
    }
}

static int mve_gdb_set_reg(CPUARMState *env, uint8_t *buf, int reg)
{
    switch (reg) {
    case 0:
        env->v7m.vpr = ldl_p(buf);
        return 4;
    default:
        return 0;
    }
}

/**
 * arm_get/set_gdb_*: get/set a gdb register
 * @env: the CPU state
 * @buf: a buffer to copy to/from
 * @reg: register number (offset from start of group)
 *
 * We return the number of bytes copied
 */

static int arm_gdb_get_sysreg(CPUARMState *env, GByteArray *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);
    const ARMCPRegInfo *ri;
    uint32_t key;

    key = cpu->dyn_sysreg_xml.data.cpregs.keys[reg];
    ri = get_arm_cp_reginfo(cpu->cp_regs, key);
    if (ri) {
        if (cpreg_field_is_64bit(ri)) {
            return gdb_get_reg64(buf, (uint64_t)read_raw_cp_reg(env, ri));
        } else {
            return gdb_get_reg32(buf, (uint32_t)read_raw_cp_reg(env, ri));
        }
    }
    return 0;
}

static int arm_gdb_set_sysreg(CPUARMState *env, uint8_t *buf, int reg)
{
    return 0;
}

static void arm_gen_one_xml_sysreg_tag(GString *s, DynamicGDBXMLInfo *dyn_xml,
                                       ARMCPRegInfo *ri, uint32_t ri_key,
                                       int bitsize, int regnum)
{
    g_string_append_printf(s, "<reg name=\"%s\"", ri->name);
    g_string_append_printf(s, " bitsize=\"%d\"", bitsize);
    g_string_append_printf(s, " regnum=\"%d\"", regnum);
    g_string_append_printf(s, " group=\"cp_regs\"/>");
    dyn_xml->data.cpregs.keys[dyn_xml->num] = ri_key;
    dyn_xml->num++;
}

static void arm_register_sysreg_for_xml(gpointer key, gpointer value,
                                        gpointer p)
{
    uint32_t ri_key = (uintptr_t)key;
    ARMCPRegInfo *ri = value;
    RegisterSysregXmlParam *param = (RegisterSysregXmlParam *)p;
    GString *s = param->s;
    ARMCPU *cpu = ARM_CPU(param->cs);
    CPUARMState *env = &cpu->env;
    DynamicGDBXMLInfo *dyn_xml = &cpu->dyn_sysreg_xml;

    if (!(ri->type & (ARM_CP_NO_RAW | ARM_CP_NO_GDB))) {
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            if (ri->state == ARM_CP_STATE_AA64) {
                arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 64,
                                           param->n++);
            }
        } else {
            if (ri->state == ARM_CP_STATE_AA32) {
                if (!arm_feature(env, ARM_FEATURE_EL3) &&
                    (ri->secure & ARM_CP_SECSTATE_S)) {
                    return;
                }
                if (ri->type & ARM_CP_64BIT) {
                    arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 64,
                                               param->n++);
                } else {
                    arm_gen_one_xml_sysreg_tag(s , dyn_xml, ri, ri_key, 32,
                                               param->n++);
                }
            }
        }
    }
}

static int arm_gen_dynamic_sysreg_xml(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    GString *s = g_string_new(NULL);
    RegisterSysregXmlParam param = {cs, s, base_reg};

    cpu->dyn_sysreg_xml.num = 0;
    cpu->dyn_sysreg_xml.data.cpregs.keys = g_new(uint32_t, g_hash_table_size(cpu->cp_regs));
    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.qemu.gdb.arm.sys.regs\">");
    g_hash_table_foreach(cpu->cp_regs, arm_register_sysreg_for_xml, &param);
    g_string_append_printf(s, "</feature>");
    cpu->dyn_sysreg_xml.desc = g_string_free(s, false);
    return cpu->dyn_sysreg_xml.num;
}

enum {
    M_SYSREG_MSP        = 0,
    M_SYSREG_PSP        = 1,
    M_SYSREG_PRIMASK    = 2,
    M_SYSREG_CONTROL    = 3,
    M_SYSREG_BASEPRI    = 4,
    M_SYSREG_FAULTMASK  = 5,
    M_SYSREG_MSPLIM     = 6,
    M_SYSREG_PSPLIM     = 7,
    M_SYSREG_REG_MASK   = 7,

    /*
     * NOTE: MSP, PSP, MSPLIM, PSPLIM technically don't exist if the
     * secure extension is present (replaced by MSP_S, MSP_NS, et al).
     * However, the MRS instruction is still allowed to read from MSP and PSP,
     * and will return the value associated with the current security state.
     * We replicate this behavior for the convenience of users, who will see
     * GDB behave similarly to their assembly code, even if they are oblivious
     * to the security extension.
     */
    M_SYSREG_CURRENT    = 0 << 3,
    M_SYSREG_NONSECURE  = 1 << 3,
    M_SYSREG_SECURE     = 2 << 3,
    M_SYSREG_MODE_MASK  = 3 << 3,
};

static const struct {
    const char *name;
    int feature;
} m_systemreg_def[] = {
    [M_SYSREG_MSP] = { "msp", ARM_FEATURE_M },
    [M_SYSREG_PSP] = { "psp", ARM_FEATURE_M },
    [M_SYSREG_PRIMASK] = { "primask", ARM_FEATURE_M },
    [M_SYSREG_CONTROL] = { "control", ARM_FEATURE_M },
    [M_SYSREG_BASEPRI] = { "basepri", ARM_FEATURE_M_MAIN },
    [M_SYSREG_FAULTMASK] = { "faultmask", ARM_FEATURE_M_MAIN },
    [M_SYSREG_MSPLIM] = { "msplim", ARM_FEATURE_V8 },
    [M_SYSREG_PSPLIM] = { "psplim", ARM_FEATURE_V8 },
};

static int arm_gdb_get_m_systemreg(CPUARMState *env, GByteArray *buf, int reg)
{
    int mode = reg & M_SYSREG_MODE_MASK;
    bool secure;
    uint32_t val;

    switch (mode) {
    case M_SYSREG_CURRENT:
        secure = env->v7m.secure;
        break;
    case M_SYSREG_NONSECURE:
        secure = false;
        break;
    case M_SYSREG_SECURE:
        secure = true;
        break;
    default:
        return 0;
    }

    reg &= M_SYSREG_REG_MASK;
    if (reg >= ARRAY_SIZE(m_systemreg_def)) {
        return 0;
    }
    if (!arm_feature(env, m_systemreg_def[reg].feature)) {
        return 0;
    }

    /* NOTE: This implementation shares a lot of logic with v7m_mrs. */
    switch (reg) {
    case M_SYSREG_MSP:
        val = *arm_v7m_get_sp_ptr(env, secure, false, true);
        break;
    case M_SYSREG_PSP:
        val = *arm_v7m_get_sp_ptr(env, secure, true, true);
        break;
    case M_SYSREG_MSPLIM:
        val = env->v7m.msplim[secure];
        break;
    case M_SYSREG_PSPLIM:
        val = env->v7m.psplim[secure];
        break;
    case M_SYSREG_PRIMASK:
        val = env->v7m.primask[secure];
        break;
    case M_SYSREG_BASEPRI:
        val = env->v7m.basepri[secure];
        break;
    case M_SYSREG_FAULTMASK:
        val = env->v7m.faultmask[secure];
        break;
    case M_SYSREG_CONTROL:
        /*
         * NOTE: CONTROL has a mix of banked and non-banked bits.
         * For "current", we emulate the MRS instruction.
         * Unfortunately, this gives GDB no way to read the SFPA bit
         * when the CPU is in a non-secure state.
         */
        if (mode == M_SYSREG_CURRENT) {
            val = arm_v7m_mrs_control(env, secure);
        } else {
            val = env->v7m.control[secure];
        }
        break;
    default:
        g_assert_not_reached();
    }

    return gdb_get_reg32(buf, val);
}

static int arm_gdb_set_m_systemreg(CPUARMState *env, uint8_t *buf, int reg)
{
    /* TODO: Implement. */
    return 0;
}

static int arm_gen_dynamic_m_systemreg_xml(CPUState *cs, int base_reg)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    GString *s = g_string_new(NULL);
    int i, ret;

    g_string_printf(s, "<?xml version=\"1.0\"?>");
    g_string_append_printf(s, "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">");
    g_string_append_printf(s, "<feature name=\"org.gnu.gdb.arm.m-system\">\n");

    QEMU_BUILD_BUG_ON(M_SYSREG_CURRENT != 0);
    ret = ARRAY_SIZE(m_systemreg_def);

    for (i = 0; i < ret; i++) {
        if (arm_feature(env, m_systemreg_def[i].feature)) {
            g_string_append_printf(s,
                "<reg name=\"%s\" bitsize=\"32\" regnum=\"%d\"/>\n",
                m_systemreg_def[i].name, base_reg + i);
        }
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        for (i = 0; i < ret; i++) {
            g_string_append_printf(s,
                "<reg name=\"%s_ns\" bitsize=\"32\" regnum=\"%d\"/>\n",
                m_systemreg_def[i].name, base_reg + (i | M_SYSREG_NONSECURE));
        }
        for (i = 0; i < ret; i++) {
            g_string_append_printf(s,
                "<reg name=\"%s_s\" bitsize=\"32\" regnum=\"%d\"/>\n",
                m_systemreg_def[i].name, base_reg + (i | M_SYSREG_SECURE));
        }
        QEMU_BUILD_BUG_ON(M_SYSREG_SECURE < M_SYSREG_NONSECURE);
        ret |= M_SYSREG_SECURE;
    }

    g_string_append_printf(s, "</feature>");

    cpu->dyn_m_systemreg_xml.desc = g_string_free(s, false);
    cpu->dyn_m_systemreg_xml.num = ret;
    return ret;
}

const char *arm_gdb_get_dynamic_xml(CPUState *cs, const char *xmlname)
{
    ARMCPU *cpu = ARM_CPU(cs);

    if (strcmp(xmlname, "system-registers.xml") == 0) {
        return cpu->dyn_sysreg_xml.desc;
    } else if (strcmp(xmlname, "sve-registers.xml") == 0) {
        return cpu->dyn_svereg_xml.desc;
    } else if (strcmp(xmlname, "arm-m-system.xml") == 0) {
        return cpu->dyn_m_systemreg_xml.desc;
    }
    return NULL;
}

void arm_cpu_register_gdb_regs_for_features(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;

    if (arm_feature(env, ARM_FEATURE_AARCH64)) {
        /*
         * The lower part of each SVE register aliases to the FPU
         * registers so we don't need to include both.
         */
#ifdef TARGET_AARCH64
        if (isar_feature_aa64_sve(&cpu->isar)) {
            int nreg = arm_gen_dynamic_svereg_xml(cs, cs->gdb_num_regs);
            gdb_register_coprocessor(cs, aarch64_gdb_get_sve_reg,
                                     aarch64_gdb_set_sve_reg, nreg,
                                     "sve-registers.xml", 0);
        } else {
            gdb_register_coprocessor(cs, aarch64_gdb_get_fpu_reg,
                                     aarch64_gdb_set_fpu_reg,
                                     34, "aarch64-fpu.xml", 0);
        }
        if (isar_feature_aa64_pauth(&cpu->isar)) {
            gdb_register_coprocessor(cs, aarch64_gdb_get_pauth_reg,
                                     aarch64_gdb_set_pauth_reg,
                                     4, "aarch64-pauth.xml", 0);
        }
#endif
    } else {
        if (arm_feature(env, ARM_FEATURE_NEON)) {
            gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                     49, "arm-neon.xml", 0);
        } else if (cpu_isar_feature(aa32_simd_r32, cpu)) {
            gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                     33, "arm-vfp3.xml", 0);
        } else if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                     17, "arm-vfp.xml", 0);
        }
        if (!arm_feature(env, ARM_FEATURE_M)) {
            /*
             * A and R profile have FP sysregs FPEXC and FPSID that we
             * expose to gdb.
             */
            gdb_register_coprocessor(cs, vfp_gdb_get_sysreg, vfp_gdb_set_sysreg,
                                     2, "arm-vfp-sysregs.xml", 0);
        }
    }
    if (cpu_isar_feature(aa32_mve, cpu)) {
        gdb_register_coprocessor(cs, mve_gdb_get_reg, mve_gdb_set_reg,
                                 1, "arm-m-profile-mve.xml", 0);
    }
    gdb_register_coprocessor(cs, arm_gdb_get_sysreg, arm_gdb_set_sysreg,
                             arm_gen_dynamic_sysreg_xml(cs, cs->gdb_num_regs),
                             "system-registers.xml", 0);

    if (arm_feature(env, ARM_FEATURE_M)) {
        gdb_register_coprocessor(cs,
            arm_gdb_get_m_systemreg, arm_gdb_set_m_systemreg,
            arm_gen_dynamic_m_systemreg_xml(cs, cs->gdb_num_regs),
            "arm-m-system.xml", 0);
    }
}
