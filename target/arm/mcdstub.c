#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/mcdstub.h"
#include "sysemu/tcg.h"
#include "internals.h"
#include "cpregs.h"
#include "mcdstub.h"

static inline int mcd_get_reg32(GByteArray *buf, uint32_t val)
{
    //FIXME: move this to a separate file
    // convert endianess if necessary
    uint32_t to_long = tswap32(val);
    g_byte_array_append(buf, (uint8_t *) &to_long, 4);
    return 4;
}

static inline int mcd_get_zeroes(GByteArray *array, size_t len)
{
    //FIXME: move this to a separate file
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

int arm_mcd_read_register(CPUState *cs, GByteArray *mem_buf, int n) {
    //CPUClass *cc = CPU_GET_CLASS(cpu);
    //CPUArchState *env = cpu->env_ptr;

    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (n < 16) {
        /* Core integer register.  */
        return mcd_get_reg32(mem_buf, env->regs[n]);
    }
    if (n < 24) {
        // TODO: these numbers don't match mine
        return mcd_get_zeroes(mem_buf, 12);
    }
    switch (n) {
    case 24:
        // TODO: these numbers don't match mine
        return mcd_get_reg32(mem_buf, 0);
    case 25:
        /* CPSR, or XPSR for M-profile */
        if (arm_feature(env, ARM_FEATURE_M)) {
            return mcd_get_reg32(mem_buf, xpsr_read(env));
        } else {
            return mcd_get_reg32(mem_buf, cpsr_read(env));
        }
    }
    //TODO: add funcitons for regs with higher numbers (including cp_regs)
    return 0;
}