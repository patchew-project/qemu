#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/mcdstub.h"
#include "sysemu/tcg.h"
#include "internals.h"
#include "cpregs.h"
#include "mcdstub.h"

static inline int mcd_get_reg32(GByteArray *buf, uint32_t val)
{
    /*
     *TODO: move this to a separate file
     *convert endianess if necessary
     */
    uint32_t to_long = tswap32(val);
    g_byte_array_append(buf, (uint8_t *) &to_long, 4);
    return 4;
}

static inline int mcd_get_reg64(GByteArray *buf, uint64_t val)
{
    uint64_t to_quad = tswap64(val);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
    return 8;
}

static inline int mcd_get_reg128(GByteArray *buf, uint64_t val_hi,
                                 uint64_t val_lo)
{
    uint64_t to_quad;
#if TARGET_BIG_ENDIAN
    to_quad = tswap64(val_hi);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
    to_quad = tswap64(val_lo);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
#else
    to_quad = tswap64(val_lo);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
    to_quad = tswap64(val_hi);
    g_byte_array_append(buf, (uint8_t *) &to_quad, 8);
#endif
    return 16;
}

static inline int mcd_get_zeroes(GByteArray *array, size_t len)
{
    /*TODO: move this to a separate file */
    guint oldlen = array->len;
    g_byte_array_set_size(array, oldlen + len);
    memset(array->data + oldlen, 0, len);

    return len;
}

const char *arm_mcd_get_dynamic_xml(CPUState *cs, const char *xmlname)
{
    ARMCPU *cpu = ARM_CPU(cs);

    if (strcmp(xmlname, "system-registers.xml") == 0) {
        return cpu->dyn_sysreg_xml.desc;
    } else if (strcmp(xmlname, "sve-registers.xml") == 0) {
        return cpu->dyn_svereg_xml.desc;
    } else if (strcmp(xmlname, "arm-m-system.xml") == 0) {
        return cpu->dyn_m_systemreg_xml.desc;
#ifndef CONFIG_USER_ONLY
    } else if (strcmp(xmlname, "arm-m-secext.xml") == 0) {
        return cpu->dyn_m_secextreg_xml.desc;
#endif
    }
    return NULL;
}

static int arm_mcd_read_gpr_register(CPUARMState *env, GByteArray *mem_buf,
    uint32_t n)
{
    if (n < 16) {
        /* Core integer register.  */
        return mcd_get_reg32(mem_buf, env->regs[n]);
    } else if (n == 16) {
        /* CPSR, or XPSR for M-profile */
        if (arm_feature(env, ARM_FEATURE_M)) {
            return mcd_get_reg32(mem_buf, xpsr_read(env));
        } else {
            return mcd_get_reg32(mem_buf, cpsr_read(env));
        }
    }
    return 0;
}

static int arm_mcd_write_gpr_register(CPUARMState *env, uint8_t *mem_buf,
    uint32_t n)
{
    uint32_t tmp;

    tmp = ldl_p(mem_buf);
    /*
     * Mask out low bits of PC
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
    } else if (n == 16) {
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
    return 0;
}

static int arm_mcd_read_vfp_register(CPUARMState *env, GByteArray *buf,
    uint32_t reg)
{
    ARMCPU *cpu = env_archcpu(env);
    int nregs = cpu_isar_feature(aa32_simd_r32, cpu) ? 32 : 16;

    /* VFP data registers are always little-endian.  */
    if (reg < nregs) {
        return mcd_get_reg64(buf, *aa32_vfp_dreg(env, reg));
    }
    if (arm_feature(env, ARM_FEATURE_NEON)) {
        /* Aliases for Q regs.  */
        nregs += 16;
        if (reg < nregs) {
            uint64_t *q = aa32_vfp_qreg(env, reg - 32);
            return mcd_get_reg128(buf, q[0], q[1]);
        }
    }
    switch (reg - nregs) {
    case 0:
        return mcd_get_reg32(buf, vfp_get_fpscr(env));
    }
    return 0;
}

static int arm_mcd_write_vfp_register(CPUARMState *env, uint8_t *buf,
    uint32_t reg)
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

static int arm_mcd_read_vfp_sys_register(CPUARMState *env, GByteArray *buf,
    uint32_t reg)
{
    switch (reg) {
    case 0:
        return mcd_get_reg32(buf, env->vfp.xregs[ARM_VFP_FPSID]);
    case 1:
        return mcd_get_reg32(buf, env->vfp.xregs[ARM_VFP_FPEXC]);
    }
    return 0;
}

static int arm_mcd_write_vfp_sys_register(CPUARMState *env, uint8_t *buf,
    uint32_t reg)
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

static int arm_mcd_read_mve_register(CPUARMState *env, GByteArray *buf,
    uint32_t reg)
{
    switch (reg) {
    case 0:
        return mcd_get_reg32(buf, env->v7m.vpr);
    default:
        return 0;
    }
}

static int arm_mcd_write_mve_register(CPUARMState *env, uint8_t *buf,
    uint32_t reg)
{
    switch (reg) {
    case 0:
        env->v7m.vpr = ldl_p(buf);
        return 4;
    default:
        return 0;
    }
}

static int arm_mcd_read_cpr_register(CPUARMState *env, GByteArray *buf,
    uint32_t reg)
{
    ARMCPU *cpu = env_archcpu(env);
    const ARMCPRegInfo *ri;
    uint32_t key;

    key = cpu->dyn_sysreg_xml.data.cpregs.keys[reg];
    ri = get_arm_cp_reginfo(cpu->cp_regs, key);
    if (ri) {
        if (cpreg_field_is_64bit(ri)) {
            return mcd_get_reg64(buf, (uint64_t)read_raw_cp_reg(env, ri));
        } else {
            return mcd_get_reg32(buf, (uint32_t)read_raw_cp_reg(env, ri));
        }
    }
    return 0;
}

static int arm_mcd_write_cpr_register(CPUARMState *env, uint8_t *buf,
    uint32_t reg)
{
    /* try write_raw_cp_reg here*/
    return 0;
}

int arm_mcd_read_register(CPUState *cs, GByteArray *mem_buf, uint8_t reg_type,
    uint32_t n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    switch (reg_type) {
    case MCD_ARM_REG_TYPE_GPR:
        return arm_mcd_read_gpr_register(env, mem_buf, n);
        break;
    case MCD_ARM_REG_TYPE_VFP:
        return arm_mcd_read_vfp_register(env, mem_buf, n);
        break;
    case MCD_ARM_REG_TYPE_VFP_SYS:
        return arm_mcd_read_vfp_sys_register(env, mem_buf, n);
        break;
    case MCD_ARM_REG_TYPE_MVE:
        return arm_mcd_read_mve_register(env, mem_buf, n);
        break;
    case MCD_ARM_REG_TYPE_CPR:
        return arm_mcd_read_cpr_register(env, mem_buf, n);
        break;
    default:
        /* unknown register type*/
        return 0;
    }
}

int arm_mcd_write_register(CPUState *cs, GByteArray *mem_buf, uint8_t reg_type,
    uint32_t n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    switch (reg_type) {
    case MCD_ARM_REG_TYPE_GPR:
        return arm_mcd_write_gpr_register(env, mem_buf->data, n);
        break;
    case MCD_ARM_REG_TYPE_VFP:
        return arm_mcd_write_vfp_register(env, mem_buf->data, n);
        break;
    case MCD_ARM_REG_TYPE_VFP_SYS:
        return arm_mcd_write_vfp_sys_register(env, mem_buf->data, n);
        break;
    case MCD_ARM_REG_TYPE_MVE:
        return arm_mcd_write_mve_register(env, mem_buf->data, n);
        break;
    case MCD_ARM_REG_TYPE_CPR:
        return arm_mcd_write_cpr_register(env, mem_buf->data, n);
        break;
    default:
        /* unknown register type*/
        return 0;
    }
}

uint16_t arm_mcd_get_opcode(CPUState *cs, uint32_t n)
{
    /*gets the opcode for a cp register*/
    ARMCPU *cpu = ARM_CPU(cs);
    const ARMCPRegInfo *ri;
    uint32_t key;

    key = cpu->dyn_sysreg_xml.data.cpregs.keys[n];
    ri = get_arm_cp_reginfo(cpu->cp_regs, key);
    if (ri) {
        uint16_t opcode = 0;
        opcode |= ri->opc1 << 14;
        opcode |= ri->opc2 << 10;
        opcode |= ri->crm << 7;
        opcode |= ri->crn << 3;
        return opcode;
    }
    return 0;
}
