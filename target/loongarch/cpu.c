/*
 * QEMU LoongArch CPU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "sysemu/qtest.h"
#include "exec/exec-all.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "qapi/qapi-commands-machine-target.h"
#include "cpu.h"
#include "cpu-csr.h"
#include "cpu-qom.h"
#include "internal.h"

static const char * const excp_names[EXCP_LAST + 1] = {
    [EXCP_INTE] = "Interrupt error",
    [EXCP_ADE] = "Address error",
    [EXCP_SYSCALL] = "Syscall",
    [EXCP_BREAK] = "Break",
    [EXCP_FPDIS] = "FPU Disabled",
    [EXCP_INE] = "Inst. Not Exist",
    [EXCP_TRAP] = "Trap",
    [EXCP_FPE] = "Floating Point Exception",
    [EXCP_TLBM] = "TLB modified fault",
    [EXCP_TLBL] = "TLB miss on a load",
    [EXCP_TLBS] = "TLB miss on a store",
    [EXCP_TLBPE] = "TLB Privilege Error",
    [EXCP_TLBXI] = "TLB Execution-Inhibit exception",
    [EXCP_TLBRI] = "TLB Read-Inhibit exception",
};

const char *loongarch_exception_name(int32_t exception)
{
    if (exception < 0 || exception > EXCP_LAST) {
        return "unknown";
    }
    return excp_names[exception];
}

target_ulong exception_resume_pc(CPULoongArchState *env)
{
    target_ulong bad_pc;

    bad_pc = env->active_tc.PC;

    return bad_pc;
}

void QEMU_NORETURN do_raise_exception_err(CPULoongArchState *env,
                                          uint32_t exception,
                                          int error_code,
                                          uintptr_t pc)
{
    CPUState *cs = env_cpu(env);

    qemu_log_mask(CPU_LOG_INT, "%s: %d (%s) %d\n",
                  __func__,
                  exception,
                  loongarch_exception_name(exception),
                  error_code);
    cs->exception_index = exception;
    env->error_code = error_code;

    cpu_loop_exit_restore(cs, pc);
}

static void loongarch_cpu_set_pc(CPUState *cs, vaddr value)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    env->active_tc.PC = value & ~(target_ulong)1;
}

bool loongarch_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        LoongArchCPU *cpu = LOONGARCH_CPU(cs);
        CPULoongArchState *env = &cpu->env;

        if (cpu_loongarch_hw_interrupts_enabled(env) &&
            cpu_loongarch_hw_interrupts_pending(env)) {
            cs->exception_index = EXCP_INTE;
            env->error_code = 0;
            loongarch_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}

void loongarch_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = EXCP_NONE;
}

#ifdef CONFIG_TCG
static void loongarch_cpu_synchronize_from_tb(CPUState *cs,
                                              const TranslationBlock *tb)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    env->active_tc.PC = tb->pc;
    env->hflags &= ~LOONGARCH_HFLAG_BMASK;
    env->hflags |= tb->flags & LOONGARCH_HFLAG_BMASK;
}
#endif /* CONFIG_TCG */

static bool loongarch_cpu_has_work(CPUState *cs)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    bool has_work = false;

    if ((cs->interrupt_request & CPU_INTERRUPT_HARD) &&
        cpu_loongarch_hw_interrupts_pending(env)) {
            has_work = true;
    }

    return has_work;
}

#define LOONGARCH_CONFIG1                                                   \
((0x8 << CSR_CONF1_KSNUM_SHIFT) | (0x2f << CSR_CONF1_TMRBITS_SHIFT) |       \
 (0x7 << CSR_CONF1_VSMAX_SHIFT))

#define LOONGARCH_CONFIG3                                                   \
((0x2 << CSR_CONF3_TLBORG_SHIFT) | (0x3f << CSR_CONF3_MTLBSIZE_SHIFT) |     \
 (0x7 << CSR_CONF3_STLBWAYS_SHIFT) | (0x8 << CSR_CONF3_STLBIDX_SHIFT))

#define LOONGARCH_MCSR0                                                     \
((0x0UL << MCSR0_GR32_SHIFT) | (0x1UL << MCSR0_GR64_SHIFT) |                \
 (0x1UL << MCSR0_PAGING_SHIFT) | (0x1UL << MCSR0_IOCSR_SHIFT) |             \
 (0x2fUL << MCSR0_PABIT_SHIFT) | (0x2fUL << MCSR0_VABIT_SHIFT) |            \
 (0x1UL << MCSR0_UAL_SHIFT) | (0x1UL << MCSR0_RI_SHIFT) |                   \
 (0x1UL << MCSR0_EXEPROT_SHIFT) | (0x1UL << MCSR0_RPLVTLB_SHIFT) |          \
 (0x1UL << MCSR0_HUGEPG_SHIFT) | (0x1UL << MCSR0_IOCSR_BRD_SHIFT) |         \
 (0x0UL << MCSR0_INT_IMPL_SHIFT) | MCSR0_PRID)

#define LOONGARCH_MCSR1                                                     \
((0x1UL << MCSR1_FP_SHIFT) | (0x1UL << MCSR1_FPSP_SHIFT) |                  \
 (0x1UL << MCSR1_FPDP_SHIFT) | (0x1UL << MCSR1_FPVERS_SHIFT) |              \
 (0x1UL << MCSR1_LSX_SHIFT) | (0x1UL << MCSR1_LASX_SHIFT) |                 \
 (0x1UL << MCSR1_COMPLEX_SHIFT) | (0x1UL << MCSR1_CRYPTO_SHIFT) |           \
 (0x0UL << MCSR1_VZ_SHIFT) | (0x0UL << MCSR1_VZVERS_SHIFT) |                \
 (0x1UL << MCSR1_LLFTP_SHIFT) | (0x1UL << MCSR1_LLFTPVERS_SHIFT) |          \
 (0x0UL << MCSR1_X86BT_SHIFT) | (0x0UL << MCSR1_ARMBT_SHIFT) |              \
 (0x0UL << MCSR1_LOONGARCHBT_SHIFT) | (0x1UL << MCSR1_LSPW_SHIFT) |         \
 (0x1UL << MCSR1_LAMO_SHIFT) | (0x1UL << MCSR1_CCDMA_SHIFT) |               \
 (0x1UL << MCSR1_SFB_SHIFT) | (0x1UL << MCSR1_UCACC_SHIFT) |                \
 (0x1UL << MCSR1_LLEXC_SHIFT) | (0x1UL << MCSR1_SCDLY_SHIFT) |              \
 (0x1UL << MCSR1_LLDBAR_SHIFT) | (0x1UL << MCSR1_ITLBT_SHIFT) |             \
 (0x1UL << MCSR1_ICACHET_SHIFT) | (0x4UL << MCSR1_SPW_LVL_SHIFT) |          \
 (0x1UL << MCSR1_HPFOLD_SHIFT))

#define LOONGARCH_MCSR2                                                     \
((0x1UL << MCSR2_CCMUL_SHIFT) | (0x1UL << MCSR2_CCDIV_SHIFT) | CCFREQ_DEFAULT)

#define LOONGARCH_MCSR3                                                     \
((0x1UL << MCSR3_PMP_SHIFT) | (0x1UL << MCSR3_PAMVER_SHIFT) |               \
 (0x3UL << MCSR3_PMNUM_SHIFT) | (0x3fUL < MCSR3_PMBITS_SHIFT) |             \
 (0x1UL << MCSR3_UPM_SHIFT))


#define LOONGARCH_MCSR8                                                     \
((0x1UL << MCSR8_L1IUPRE_SHIFT)   | (0x0 << MCSR8_L1IUUNIFY_SHIFT) |        \
 (0x1UL << MCSR8_L1DPRE_SHIFT)    | (0x1UL << MCSR8_L2IUPRE_SHIFT) |        \
 (0x1UL << MCSR8_L2IUUNIFY_SHIFT) | (0x1UL << MCSR8_L2IUPRIV_SHIFT) |       \
 (0x0 << MCSR8_L2IUINCL_SHIFT)    | (0x0 << MCSR8_L2DPRE_SHIFT) |           \
 (0x0 << MCSR8_L2DPRIV_SHIFT)     | (0x0 << MCSR8_L2DINCL_SHIFT) |          \
 (0x1UL << MCSR8_L3IUPRE_SHIFT)   | (0x1UL << MCSR8_L3IUUNIFY_SHIFT) |      \
 (0x0 << MCSR8_L3IUPRIV_SHIFT)    | (0x1UL << MCSR8_L3IUINCL_SHIFT) |       \
 (0x0 << MCSR8_L3DPRE_SHIFT)      | (0x0 < MCSR8_L3DPRIV_SHIFT) |           \
 (0x0 << MCSR8_L3DINCL_SHIFT)     | (0x3UL << MCSR8_L1I_WAY_SHIFT) |        \
 (0x8UL << MCSR8_L1I_IDX_SHIFT)   | (0x6UL << MCSR8_L1I_SIZE_SHIFT))

#define LOONGARCH_MCSR9                                                     \
((0x3UL << MCSR9_L1D_WAY_SHIFT) | (0x8UL << MCSR9_L1D_IDX_SHIFT) |          \
 (0x6UL << MCSR9_L1D_SIZE_SHIFT) | (0xfUL << MCSR9_L2U_WAY_SHIFT) |         \
 (0x8UL << MCSR9_L2U_IDX_SHIFT) |  (0x6UL << MCSR9_L2U_SIZE_SHIFT))

#define LOONGARCH_MCSR10                                                    \
((0xfUL << MCSR10_L3U_WAY_SHIFT) | (0xfUL << MCSR10_L3U_IDX_SHIFT) |        \
 (0x6UL << MCSR10_L3U_SIZE_SHIFT))

#define LOONGARCH_MCSR24                                                    \
((0x0 << MCSR24_MCSRLOCK_SHIFT) | (0x0 << MCSR24_NAPEN_SHIFT) |             \
 (0x0 << MCSR24_VFPUCG_SHIFT) | (0x0 << MCSR24_RAMCG_SHIFT))

/* LoongArch CPU definitions */
const loongarch_def_t loongarch_defs[] = {
    {
        .name = "Loongson-3A5000",

        /* for LoongArch CSR */
        .CSR_PRCFG1 = LOONGARCH_CONFIG1,
        .CSR_PRCFG2 = 0x3ffff000,
        .CSR_PRCFG3 = LOONGARCH_CONFIG3,
        .CSR_CRMD = (0 << CSR_CRMD_PLV_SHIFT) | (0 << CSR_CRMD_IE_SHIFT) |
                    (1 << CSR_CRMD_DA_SHIFT) | (0 << CSR_CRMD_PG_SHIFT) |
                    (1 << CSR_CRMD_DACF_SHIFT) | (1 << CSR_CRMD_DACM_SHIFT),
        .CSR_ECFG = 0x7 << 16,
        .CSR_STLBPGSIZE  = 0xe,
        .CSR_RVACFG = 0x0,
        .CSR_MCSR0 = LOONGARCH_MCSR0,
        .CSR_MCSR1 = LOONGARCH_MCSR1,
        .CSR_MCSR2 = LOONGARCH_MCSR2,
        .CSR_MCSR3 = 0,
        .CSR_MCSR8 = LOONGARCH_MCSR8,
        .CSR_MCSR9 = LOONGARCH_MCSR9,
        .CSR_MCSR10 = LOONGARCH_MCSR10,
        .CSR_MCSR24 = LOONGARCH_MCSR24,
        .FCSR0 = 0x0,
        .FCSR0_MASK = 0x1f1f03df,
        .PABITS = 48,
        .INSN_FLAGS = CPU_LA64 | INSN_LOONGARCH,
        .MMU_TYPE = MMU_TYPE_LS3A5K,
    },
    {
        .name = "host",

        /* for LoongArch CSR */
        .CSR_PRCFG1 = LOONGARCH_CONFIG1,
        .CSR_PRCFG2 = 0x3ffff000,
        .CSR_PRCFG3 = LOONGARCH_CONFIG3,
        .CSR_CRMD = (0 << CSR_CRMD_PLV_SHIFT) | (0 << CSR_CRMD_IE_SHIFT) |
                    (1 << CSR_CRMD_DA_SHIFT) | (0 << CSR_CRMD_PG_SHIFT) |
                    (1 << CSR_CRMD_DACF_SHIFT) | (1 << CSR_CRMD_DACM_SHIFT),
        .CSR_ECFG = 0x7 << 16,
        .CSR_STLBPGSIZE  = 0xe,
        .CSR_RVACFG = 0x0,
        .CSR_MCSR0 = LOONGARCH_MCSR0,
        .CSR_MCSR1 = LOONGARCH_MCSR1,
        .CSR_MCSR2 = LOONGARCH_MCSR2,
        .CSR_MCSR3 = 0,
        .CSR_MCSR8 = LOONGARCH_MCSR8,
        .CSR_MCSR9 = LOONGARCH_MCSR9,
        .CSR_MCSR10 = LOONGARCH_MCSR10,
        .CSR_MCSR24 = LOONGARCH_MCSR24,
        .FCSR0 = 0x0,
        .FCSR0_MASK = 0x1f1f03df,
        .PABITS = 48,
        .INSN_FLAGS = CPU_LA64 | INSN_LOONGARCH,
        .MMU_TYPE = MMU_TYPE_LS3A5K,
    },
};

const int loongarch_defs_number = ARRAY_SIZE(loongarch_defs);

void loongarch_cpu_list(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(loongarch_defs); i++) {
        qemu_printf("LoongArch '%s'\n", loongarch_defs[i].name);
    }
}

static void fpu_init(CPULoongArchState *env, const loongarch_def_t *def)
{
    memcpy(&env->active_fpu, &env->fpus[0], sizeof(env->active_fpu));
}

static void loongarch_cpu_reset(DeviceState *dev)
{
    CPUState *cs = CPU(dev);
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    LoongArchCPUClass *mcc = LOONGARCH_CPU_GET_CLASS(cpu);
    CPULoongArchState *env = &cpu->env;

    mcc->parent_reset(dev);

    memset(env, 0, offsetof(CPULoongArchState, end_reset_fields));

    /* Reset registers to their default values */
    env->CSR_PRCFG1 = env->cpu_model->CSR_PRCFG1;
    env->CSR_PRCFG2 = env->cpu_model->CSR_PRCFG2;
    env->CSR_PRCFG3 = env->cpu_model->CSR_PRCFG3;
    env->CSR_CRMD = env->cpu_model->CSR_CRMD;
    env->CSR_ECFG = env->cpu_model->CSR_ECFG;
    env->CSR_STLBPGSIZE = env->cpu_model->CSR_STLBPGSIZE;
    env->CSR_RVACFG = env->cpu_model->CSR_RVACFG;
    env->CSR_MCSR0 = env->cpu_model->CSR_MCSR0;
    env->CSR_MCSR1 = env->cpu_model->CSR_MCSR1;
    env->CSR_MCSR2 = env->cpu_model->CSR_MCSR2;
    env->CSR_MCSR3 = env->cpu_model->CSR_MCSR3;
    env->CSR_MCSR8 = env->cpu_model->CSR_MCSR8;
    env->CSR_MCSR9 = env->cpu_model->CSR_MCSR9;
    env->CSR_MCSR10 = env->cpu_model->CSR_MCSR10;
    env->CSR_MCSR24 = env->cpu_model->CSR_MCSR24;

    env->current_tc = 0;
    env->PABITS = env->cpu_model->PABITS;
    env->active_fpu.fcsr0_mask = env->cpu_model->FCSR0_MASK;
    env->active_fpu.fcsr0 = env->cpu_model->FCSR0;
    env->insn_flags = env->cpu_model->INSN_FLAGS;

    compute_hflags(env);
    restore_pamask(env);
    cs->exception_index = EXCP_NONE;
}

static void loongarch_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    info->print_insn = print_insn_loongarch;
}

static void loongarch_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    LoongArchCPU *cpu = LOONGARCH_CPU(dev);
    CPULoongArchState *env = &cpu->env;
    LoongArchCPUClass *mcc = LOONGARCH_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    env->exception_base = 0x1C000000;

    fpu_init(env, env->cpu_model);

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    mcc->parent_realize(dev, errp);
}

static void loongarch_cpu_initfn(Object *obj)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);
    CPULoongArchState *env = &cpu->env;
    LoongArchCPUClass *mcc = LOONGARCH_CPU_GET_CLASS(obj);

    cpu_set_cpustate_pointers(cpu);
    cpu->clock = qdev_init_clock_in(DEVICE(obj), "clk-in", NULL, cpu, 0);
    env->cpu_model = mcc->cpu_def;
}

static char *loongarch_cpu_type_name(const char *cpu_model)
{
    return g_strdup_printf(LOONGARCH_CPU_TYPE_NAME("%s"), cpu_model);
}

static ObjectClass *loongarch_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = loongarch_cpu_type_name(cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    return oc;
}

static Property loongarch_cpu_properties[] = {
    DEFINE_PROP_INT32("core-id", LoongArchCPU, core_id, -1),
    DEFINE_PROP_UINT32("id", LoongArchCPU, id, UNASSIGNED_CPU_ID),
    DEFINE_PROP_INT32("node-id", LoongArchCPU, node_id, CPU_UNSET_NUMA_NODE_ID),
    DEFINE_PROP_END_OF_LIST()
};

#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"

static struct TCGCPUOps loongarch_tcg_ops = {
    .initialize = loongarch_tcg_init,
    .synchronize_from_tb = loongarch_cpu_synchronize_from_tb,
    .cpu_exec_interrupt = loongarch_cpu_exec_interrupt,
};
#endif /* CONFIG_TCG */

static void loongarch_cpu_class_init(ObjectClass *c, void *data)
{
    LoongArchCPUClass *mcc = LOONGARCH_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    device_class_set_parent_realize(dc, loongarch_cpu_realizefn,
                                    &mcc->parent_realize);
    device_class_set_parent_reset(dc, loongarch_cpu_reset, &mcc->parent_reset);
    device_class_set_props(dc, loongarch_cpu_properties);

    cc->class_by_name = loongarch_cpu_class_by_name;
    cc->has_work = loongarch_cpu_has_work;
    cc->dump_state = loongarch_cpu_dump_state;
    cc->set_pc = loongarch_cpu_set_pc;
    cc->disas_set_info = loongarch_cpu_disas_set_info;
#ifdef CONFIG_TCG
    cc->tcg_ops = &loongarch_tcg_ops;
#endif
}

static const TypeInfo loongarch_cpu_type_info = {
    .name = TYPE_LOONGARCH_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(LoongArchCPU),
    .instance_init = loongarch_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(LoongArchCPUClass),
    .class_init = loongarch_cpu_class_init,
};

static void loongarch_cpu_cpudef_class_init(ObjectClass *oc, void *data)
{
    LoongArchCPUClass *mcc = LOONGARCH_CPU_CLASS(oc);
    mcc->cpu_def = data;
}

static void loongarch_register_cpudef_type(const struct loongarch_def_t *def)
{
    char *typename = loongarch_cpu_type_name(def->name);
    TypeInfo ti = {
        .name = typename,
        .parent = TYPE_LOONGARCH_CPU,
        .class_init = loongarch_cpu_cpudef_class_init,
        .class_data = (void *)def,
    };

    type_register(&ti);
    g_free(typename);
}

static void loongarch_cpu_register_types(void)
{
    int i;

    type_register_static(&loongarch_cpu_type_info);
    for (i = 0; i < loongarch_defs_number; i++) {
        loongarch_register_cpudef_type(&loongarch_defs[i]);
    }
}

type_init(loongarch_cpu_register_types)

static void loongarch_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info;
    const char *typename;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = g_strndup(typename,
                           strlen(typename) - strlen("-" TYPE_LOONGARCH_CPU));
    info->q_typename = g_strdup(typename);

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;

    list = object_class_get_list(TYPE_LOONGARCH_CPU, false);
    g_slist_foreach(list, loongarch_cpu_add_definition, &cpu_list);
    g_slist_free(list);

    return cpu_list;
}
