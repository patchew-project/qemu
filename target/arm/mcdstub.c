#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/mcdstub.h"
#include "sysemu/tcg.h"
#include "internals.h"
#include "cpregs.h"
#include "mcdstub.h"

static inline int mcd_get_reg32(GByteArray *buf, uint32_t val)
{
    //TODO: move this to a separate file
    // convert endianess if necessary
    uint32_t to_long = tswap32(val);
    g_byte_array_append(buf, (uint8_t *) &to_long, 4);
    return 4;
}

static inline int mcd_get_zeroes(GByteArray *array, size_t len)
{
    //TODO: move this to a separate file
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
    //TODO: add funcitons for the remaining regs (including cp_regs)
    return 0;
}

int arm_mcd_write_register(CPUState *cs, GByteArray *mem_buf, int n) {
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t tmp;

    tmp = ldl_p(mem_buf);
    tmp = *((uint32_t*)mem_buf->data);

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
        return 4;
    }
    switch (n) {
    case 24:
        /* FPA status register (ignored).  */
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
    //TODO: add funcitons for the remaining regs (including cp_regs)
    return 0;
}
