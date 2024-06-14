#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpregs.h"
#include "chardev/char-fe.h"

#define MDCCSR_EL0_RXFULL_MASK (1 << 30)
#define MDCCSR_EL0_TXFULL_MASK (1 << 29)

static void debug_dcc_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    ARMCPU *cpu = ri->opaque;
    env->cp15.dbgdtr_tx = value;

    if (qemu_chr_fe_get_driver(&cpu->dcc)) {
        /*
         * Usually dcc is used for putc/getc calls which expect only
         * 1 byte from external debugger.
         * TODO: This needs to be generalized for other use-cases.
         */
        qemu_chr_fe_write_all(&cpu->dcc, (uint8_t *)&env->cp15.dbgdtr_tx, 1);
    }
}

static uint64_t debug_dcc_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint32_t ret = 0;
    ARMCPU *cpu = ri->opaque;

    if (env->cp15.mdscr_el1 & MDCCSR_EL0_RXFULL_MASK) {
        ret = env->cp15.dbgdtr_rx;
        env->cp15.dbgdtr_rx = 0;
        env->cp15.mdscr_el1 &= ~MDCCSR_EL0_RXFULL_MASK;
        qemu_chr_fe_accept_input(&cpu->dcc);
    }
    return ret;
}

static const ARMCPRegInfo dcc_cp_reginfo[] = {
    /* DBGDTRTX_EL0/DBGDTRRX_EL0 depend on direction */
    { .name = "DBGDTR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 2, .opc1 = 3, .crn = 0, .crm = 5, .opc2 = 0,
      .access = PL0_RW, .writefn = debug_dcc_write,
      .readfn = debug_dcc_read,
      .type = ARM_CP_OVERRIDE, .resetvalue = 0 },
    /* DBGDTRTXint/DBGDTRRXint depend on direction */
    { .name = "DBGDTRint", .state = ARM_CP_STATE_AA32, .cp = 14,
      .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 0,
      .access = PL0_RW, .writefn = debug_dcc_write,
      .readfn = debug_dcc_read,
      .type = ARM_CP_OVERRIDE, .resetvalue = 0 },
};


static int dcc_chr_can_read(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    if (!(env->cp15.mdscr_el1 & MDCCSR_EL0_RXFULL_MASK)) {
        /*
         * Usually dcc is used for putc/getc calls which expect only
         * 1 byte from external debugger.
         * TODO: This needs to be generalized for other use-cases.
         */
        return 1;
    }

    return 0;
}

static void dcc_chr_read(void *opaque, const uint8_t *buf, int size)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;

    env->cp15.dbgdtr_rx = *buf;
    env->cp15.mdscr_el1 |= MDCCSR_EL0_RXFULL_MASK;
}

void arm_dcc_init(ARMCPU *cpu)
{
    Chardev *chr;
    char *dcc_name;
    CPUState *p = CPU(cpu);

    dcc_name = g_strdup_printf("dcc%d", p->cpu_index);
    chr = qemu_chr_find(dcc_name);
    define_arm_cp_regs_with_opaque(cpu, dcc_cp_reginfo, cpu);
    if (chr) {
        qemu_chr_fe_init(&cpu->dcc, chr, NULL);
        qemu_chr_fe_set_handlers(&cpu->dcc,
                      dcc_chr_can_read,
                      dcc_chr_read,
                      NULL, NULL, cpu, NULL, true);
    }
    g_free(dcc_name);
}
