/*
 *  PowerPC CPU routines for qemu.
 *
 * Copyright (c) 2017 Nikunj A Dadhania, IBM Corporation.
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
#include "cpu-models.h"
#include "kvm_ppc.h"
#include "qemu/qemu-print.h"
#include "qemu/cutils.h"
#include "qapi/qapi-commands-machine-target.h"

#include "qapi/error.h"
#include "sysemu/tcg.h"
#include "helper_regs.h"
#include "hw/ppc/ppc.h"
#include "fpu/softfloat-helpers.h"
#include "sysemu/hw_accel.h"
#include "disas/capstone.h"
#include "hw/qdev-properties.h"
#include "internal.h"
#include "mmu-hash64.h"

target_ulong cpu_read_xer(CPUPPCState *env)
{
    if (is_isa300(env)) {
        return env->xer | (env->so << XER_SO) |
            (env->ov << XER_OV) | (env->ca << XER_CA) |
            (env->ov32 << XER_OV32) | (env->ca32 << XER_CA32);
    }

    return env->xer | (env->so << XER_SO) | (env->ov << XER_OV) |
        (env->ca << XER_CA);
}

void cpu_write_xer(CPUPPCState *env, target_ulong xer)
{
    env->so = (xer >> XER_SO) & 1;
    env->ov = (xer >> XER_OV) & 1;
    env->ca = (xer >> XER_CA) & 1;
    /* write all the flags, while reading back check of isa300 */
    env->ov32 = (xer >> XER_OV32) & 1;
    env->ca32 = (xer >> XER_CA32) & 1;
    env->xer = xer & ~((1ul << XER_SO) |
                       (1ul << XER_OV) | (1ul << XER_CA) |
                       (1ul << XER_OV32) | (1ul << XER_CA32));
}


static const char *ppc_cpu_lookup_alias(const char *alias)
{
    int ai;

    for (ai = 0; ppc_cpu_aliases[ai].alias != NULL; ai++) {
        if (strcmp(ppc_cpu_aliases[ai].alias, alias) == 0) {
            return ppc_cpu_aliases[ai].model;
        }
    }

    return NULL;
}

static gint ppc_cpu_compare_class_pvr(gconstpointer a, gconstpointer b)
{
    ObjectClass *oc = (ObjectClass *)a;
    uint32_t pvr = *(uint32_t *)b;
    PowerPCCPUClass *pcc = (PowerPCCPUClass *)a;

    /* -cpu host does a PVR lookup during construction */
    if (unlikely(strcmp(object_class_get_name(oc),
                        TYPE_HOST_POWERPC_CPU) == 0)) {
        return -1;
    }

    return pcc->pvr == pvr ? 0 : -1;
}

static gint ppc_cpu_compare_class_pvr_mask(gconstpointer a, gconstpointer b)
{
    ObjectClass *oc = (ObjectClass *)a;
    uint32_t pvr = *(uint32_t *)b;
    PowerPCCPUClass *pcc = (PowerPCCPUClass *)a;

    /* -cpu host does a PVR lookup during construction */
    if (unlikely(strcmp(object_class_get_name(oc),
                        TYPE_HOST_POWERPC_CPU) == 0)) {
        return -1;
    }

    if (pcc->pvr_match(pcc, pvr)) {
        return 0;
    }

    return -1;
}

PowerPCCPUClass *ppc_cpu_class_by_pvr(uint32_t pvr)
{
    GSList *list, *item;
    PowerPCCPUClass *pcc = NULL;

    list = object_class_get_list(TYPE_POWERPC_CPU, false);
    item = g_slist_find_custom(list, &pvr, ppc_cpu_compare_class_pvr);
    if (item != NULL) {
        pcc = POWERPC_CPU_CLASS(item->data);
    }
    g_slist_free(list);

    return pcc;
}

PowerPCCPUClass *ppc_cpu_class_by_pvr_mask(uint32_t pvr)
{
    GSList *list, *item;
    PowerPCCPUClass *pcc = NULL;

    list = object_class_get_list(TYPE_POWERPC_CPU, true);
    item = g_slist_find_custom(list, &pvr, ppc_cpu_compare_class_pvr_mask);
    if (item != NULL) {
        pcc = POWERPC_CPU_CLASS(item->data);
    }
    g_slist_free(list);

    return pcc;
}

PowerPCCPUClass *ppc_cpu_get_family_class(PowerPCCPUClass *pcc)
{
    ObjectClass *oc = OBJECT_CLASS(pcc);

    while (oc && !object_class_is_abstract(oc)) {
        oc = object_class_get_parent(oc);
    }
    assert(oc);

    return POWERPC_CPU_CLASS(oc);
}

/* Sort by PVR, ordering special case "host" last. */
static gint ppc_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *oc_a = (ObjectClass *)a;
    ObjectClass *oc_b = (ObjectClass *)b;
    PowerPCCPUClass *pcc_a = POWERPC_CPU_CLASS(oc_a);
    PowerPCCPUClass *pcc_b = POWERPC_CPU_CLASS(oc_b);
    const char *name_a = object_class_get_name(oc_a);
    const char *name_b = object_class_get_name(oc_b);

    if (strcmp(name_a, TYPE_HOST_POWERPC_CPU) == 0) {
        return 1;
    } else if (strcmp(name_b, TYPE_HOST_POWERPC_CPU) == 0) {
        return -1;
    } else {
        /* Avoid an integer overflow during subtraction */
        if (pcc_a->pvr < pcc_b->pvr) {
            return -1;
        } else if (pcc_a->pvr > pcc_b->pvr) {
            return 1;
        } else {
            return 0;
        }
    }
}
#ifndef CONFIG_USER_ONLY
static const TypeInfo ppc_vhyp_type_info = {
    .name = TYPE_PPC_VIRTUAL_HYPERVISOR,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(PPCVirtualHypervisorClass),
};
#endif

static ObjectClass *ppc_cpu_class_by_name(const char *name)
{
    char *cpu_model, *typename;
    ObjectClass *oc;
    const char *p;
    unsigned long pvr;

    /*
     * Lookup by PVR if cpu_model is valid 8 digit hex number (excl:
     * 0x prefix if present)
     */
    if (!qemu_strtoul(name, &p, 16, &pvr)) {
        int len = p - name;
        len = (len == 10) && (name[1] == 'x') ? len - 2 : len;
        if ((len == 8) && (*p == '\0')) {
            return OBJECT_CLASS(ppc_cpu_class_by_pvr(pvr));
        }
    }

    cpu_model = g_ascii_strdown(name, -1);
    p = ppc_cpu_lookup_alias(cpu_model);
    if (p) {
        g_free(cpu_model);
        cpu_model = g_strdup(p);
    }

    typename = g_strdup_printf("%s" POWERPC_CPU_TYPE_SUFFIX, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    g_free(cpu_model);

    return oc;
}

static bool ppc_cpu_interrupts_big_endian_always(PowerPCCPU *cpu)
{
    return true;
}

static int ppc_fixup_cpu(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    /*
     * TCG doesn't (yet) emulate some groups of instructions that are
     * implemented on some otherwise supported CPUs (e.g. VSX and
     * decimal floating point instructions on POWER7).  We remove
     * unsupported instruction groups from the cpu state's instruction
     * masks and hope the guest can cope.  For at least the pseries
     * machine, the unavailability of these instructions can be
     * advertised to the guest via the device tree.
     */
    if ((env->insns_flags & ~PPC_TCG_INSNS)
        || (env->insns_flags2 & ~PPC_TCG_INSNS2)) {
        warn_report("Disabling some instructions which are not "
                    "emulated by TCG (0x%" PRIx64 ", 0x%" PRIx64 ")",
                    env->insns_flags & ~PPC_TCG_INSNS,
                    env->insns_flags2 & ~PPC_TCG_INSNS2);
    }
    env->insns_flags &= PPC_TCG_INSNS;
    env->insns_flags2 &= PPC_TCG_INSNS2;
    return 0;
}

static void ppc_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    PowerPCCPU *cpu = POWERPC_CPU(dev);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
    if (cpu->vcpu_id == UNASSIGNED_CPU_INDEX) {
        cpu->vcpu_id = cs->cpu_index;
    }

    if (tcg_enabled()) {
        if (ppc_fixup_cpu(cpu) != 0) {
            error_setg(errp, "Unable to emulate selected CPU with TCG");
            goto unrealize;
        }
    }

    create_ppc_opcodes(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        goto unrealize;
    }
    init_ppc_proc(cpu);

    ppc_cpu_gdb_init(cs, pcc);

    qemu_init_vcpu(cs);

    pcc->parent_realize(dev, errp);

#if defined(PPC_DUMP_CPU)
    {
        CPUPPCState *env = &cpu->env;
        const char *mmu_model, *excp_model, *bus_model;
        switch (env->mmu_model) {
        case POWERPC_MMU_32B:
            mmu_model = "PowerPC 32";
            break;
        case POWERPC_MMU_SOFT_6xx:
            mmu_model = "PowerPC 6xx/7xx with software driven TLBs";
            break;
        case POWERPC_MMU_SOFT_74xx:
            mmu_model = "PowerPC 74xx with software driven TLBs";
            break;
        case POWERPC_MMU_SOFT_4xx:
            mmu_model = "PowerPC 4xx with software driven TLBs";
            break;
        case POWERPC_MMU_SOFT_4xx_Z:
            mmu_model = "PowerPC 4xx with software driven TLBs "
                "and zones protections";
            break;
        case POWERPC_MMU_REAL:
            mmu_model = "PowerPC real mode only";
            break;
        case POWERPC_MMU_MPC8xx:
            mmu_model = "PowerPC MPC8xx";
            break;
        case POWERPC_MMU_BOOKE:
            mmu_model = "PowerPC BookE";
            break;
        case POWERPC_MMU_BOOKE206:
            mmu_model = "PowerPC BookE 2.06";
            break;
        case POWERPC_MMU_601:
            mmu_model = "PowerPC 601";
            break;
#if defined(TARGET_PPC64)
        case POWERPC_MMU_64B:
            mmu_model = "PowerPC 64";
            break;
#endif
        default:
            mmu_model = "Unknown or invalid";
            break;
        }
        switch (env->excp_model) {
        case POWERPC_EXCP_STD:
            excp_model = "PowerPC";
            break;
        case POWERPC_EXCP_40x:
            excp_model = "PowerPC 40x";
            break;
        case POWERPC_EXCP_601:
            excp_model = "PowerPC 601";
            break;
        case POWERPC_EXCP_602:
            excp_model = "PowerPC 602";
            break;
        case POWERPC_EXCP_603:
            excp_model = "PowerPC 603";
            break;
        case POWERPC_EXCP_603E:
            excp_model = "PowerPC 603e";
            break;
        case POWERPC_EXCP_604:
            excp_model = "PowerPC 604";
            break;
        case POWERPC_EXCP_7x0:
            excp_model = "PowerPC 740/750";
            break;
        case POWERPC_EXCP_7x5:
            excp_model = "PowerPC 745/755";
            break;
        case POWERPC_EXCP_74xx:
            excp_model = "PowerPC 74xx";
            break;
        case POWERPC_EXCP_BOOKE:
            excp_model = "PowerPC BookE";
            break;
#if defined(TARGET_PPC64)
        case POWERPC_EXCP_970:
            excp_model = "PowerPC 970";
            break;
#endif
        default:
            excp_model = "Unknown or invalid";
            break;
        }
        switch (env->bus_model) {
        case PPC_FLAGS_INPUT_6xx:
            bus_model = "PowerPC 6xx";
            break;
        case PPC_FLAGS_INPUT_BookE:
            bus_model = "PowerPC BookE";
            break;
        case PPC_FLAGS_INPUT_405:
            bus_model = "PowerPC 405";
            break;
        case PPC_FLAGS_INPUT_401:
            bus_model = "PowerPC 401/403";
            break;
        case PPC_FLAGS_INPUT_RCPU:
            bus_model = "RCPU / MPC8xx";
            break;
#if defined(TARGET_PPC64)
        case PPC_FLAGS_INPUT_970:
            bus_model = "PowerPC 970";
            break;
#endif
        default:
            bus_model = "Unknown or invalid";
            break;
        }
        printf("PowerPC %-12s : PVR %08x MSR %016" PRIx64 "\n"
               "    MMU model        : %s\n",
               object_class_get_name(OBJECT_CLASS(pcc)),
               pcc->pvr, pcc->msr_mask, mmu_model);
#if !defined(CONFIG_USER_ONLY)
        if (env->tlb.tlb6) {
            printf("                       %d %s TLB in %d ways\n",
                   env->nb_tlb, env->id_tlbs ? "splitted" : "merged",
                   env->nb_ways);
        }
#endif
        printf("    Exceptions model : %s\n"
               "    Bus model        : %s\n",
               excp_model, bus_model);
        printf("    MSR features     :\n");
        if (env->flags & POWERPC_FLAG_SPE) {
            printf("                        signal processing engine enable"
                   "\n");
        } else if (env->flags & POWERPC_FLAG_VRE) {
            printf("                        vector processor enable\n");
        }
        if (env->flags & POWERPC_FLAG_TGPR) {
            printf("                        temporary GPRs\n");
        } else if (env->flags & POWERPC_FLAG_CE) {
            printf("                        critical input enable\n");
        }
        if (env->flags & POWERPC_FLAG_SE) {
            printf("                        single-step trace mode\n");
        } else if (env->flags & POWERPC_FLAG_DWE) {
            printf("                        debug wait enable\n");
        } else if (env->flags & POWERPC_FLAG_UBLE) {
            printf("                        user BTB lock enable\n");
        }
        if (env->flags & POWERPC_FLAG_BE) {
            printf("                        branch-step trace mode\n");
        } else if (env->flags & POWERPC_FLAG_DE) {
            printf("                        debug interrupt enable\n");
        }
        if (env->flags & POWERPC_FLAG_PX) {
            printf("                        inclusive protection\n");
        } else if (env->flags & POWERPC_FLAG_PMM) {
            printf("                        performance monitor mark\n");
        }
        if (env->flags == POWERPC_FLAG_NONE) {
            printf("                        none\n");
        }
        printf("    Time-base/decrementer clock source: %s\n",
               env->flags & POWERPC_FLAG_RTC_CLK ? "RTC clock" : "bus clock");
        dump_ppc_insns(env);
        dump_ppc_sprs(env);
        fflush(stdout);
    }
#endif
    return;

unrealize:
    cpu_exec_unrealizefn(cs);
}

static void ppc_cpu_unrealize(DeviceState *dev)
{
    PowerPCCPU *cpu = POWERPC_CPU(dev);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    pcc->parent_unrealize(dev);

    cpu_remove_sync(CPU(cpu));

    destroy_ppc_opcodes(cpu);
}

static void ppc_cpu_set_pc(CPUState *cs, vaddr value)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    cpu->env.nip = value;
}

static bool ppc_cpu_has_work(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    return msr_ee && (cs->interrupt_request & CPU_INTERRUPT_HARD);
}

static void ppc_cpu_reset(DeviceState *dev)
{
    CPUState *s = CPU(dev);
    PowerPCCPU *cpu = POWERPC_CPU(s);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;
    target_ulong msr;
    int i;

    pcc->parent_reset(dev);

    msr = (target_ulong)0;
    msr |= (target_ulong)MSR_HVB;
    msr |= (target_ulong)0 << MSR_AP; /* TO BE CHECKED */
    msr |= (target_ulong)0 << MSR_SA; /* TO BE CHECKED */
    msr |= (target_ulong)1 << MSR_EP;
#if defined(DO_SINGLE_STEP) && 0
    /* Single step trace mode */
    msr |= (target_ulong)1 << MSR_SE;
    msr |= (target_ulong)1 << MSR_BE;
#endif
#if defined(CONFIG_USER_ONLY)
    msr |= (target_ulong)1 << MSR_FP; /* Allow floating point usage */
    msr |= (target_ulong)1 << MSR_FE0; /* Allow floating point exceptions */
    msr |= (target_ulong)1 << MSR_FE1;
    msr |= (target_ulong)1 << MSR_VR; /* Allow altivec usage */
    msr |= (target_ulong)1 << MSR_VSX; /* Allow VSX usage */
    msr |= (target_ulong)1 << MSR_SPE; /* Allow SPE usage */
    msr |= (target_ulong)1 << MSR_PR;
#if defined(TARGET_PPC64)
    msr |= (target_ulong)1 << MSR_TM; /* Transactional memory */
#endif
#if !defined(TARGET_WORDS_BIGENDIAN)
    msr |= (target_ulong)1 << MSR_LE; /* Little-endian user mode */
    if (!((env->msr_mask >> MSR_LE) & 1)) {
        fprintf(stderr, "Selected CPU does not support little-endian.\n");
        exit(1);
    }
#endif
#endif

#if defined(TARGET_PPC64)
    if (mmu_is_64bit(env->mmu_model)) {
        msr |= (1ULL << MSR_SF);
    }
#endif

    hreg_store_msr(env, msr, 1);

#if !defined(CONFIG_USER_ONLY)
    env->nip = env->hreset_vector | env->excp_prefix;
    if (env->mmu_model != POWERPC_MMU_REAL) {
        ppc_tlb_invalidate_all(env);
    }
#endif

    hreg_compute_hflags(env);
    env->reserve_addr = (target_ulong)-1ULL;
    /* Be sure no exception or interrupt is pending */
    env->pending_interrupts = 0;
    s->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
    ppc_irq_reset(cpu);

    /* tininess for underflow is detected before rounding */
    set_float_detect_tininess(float_tininess_before_rounding,
                              &env->fp_status);

    for (i = 0; i < ARRAY_SIZE(env->spr_cb); i++) {
        ppc_spr_t *spr = &env->spr_cb[i];

        if (!spr->name) {
            continue;
        }
        env->spr[i] = spr->default_value;
    }
}

#ifndef CONFIG_USER_ONLY

static bool ppc_cpu_is_big_endian(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    cpu_synchronize_state(cs);

    return !msr_le;
}

#ifdef CONFIG_TCG
static void ppc_cpu_exec_enter(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->cpu_exec_enter(cpu->vhyp, cpu);
    }
}

static void ppc_cpu_exec_exit(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        vhc->cpu_exec_exit(cpu->vhyp, cpu);
    }
}
#endif /* CONFIG_TCG */

#endif /* !CONFIG_USER_ONLY */

static bool ppc_pvr_match_default(PowerPCCPUClass *pcc, uint32_t pvr)
{
    return pcc->pvr == pvr;
}

static gchar *ppc_gdb_arch_name(CPUState *cs)
{
#if defined(TARGET_PPC64)
    return g_strdup("powerpc:common64");
#else
    return g_strdup("powerpc:common");
#endif
}

static void ppc_disas_set_info(CPUState *cs, disassemble_info *info)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;

    if ((env->hflags >> MSR_LE) & 1) {
        info->endian = BFD_ENDIAN_LITTLE;
    }
    info->mach = env->bfd_mach;
    if (!env->bfd_mach) {
#ifdef TARGET_PPC64
        info->mach = bfd_mach_ppc64;
#else
        info->mach = bfd_mach_ppc;
#endif
    }
    info->disassembler_options = (char *)"any";
    info->print_insn = print_insn_ppc;

    info->cap_arch = CS_ARCH_PPC;
#ifdef TARGET_PPC64
    info->cap_mode = CS_MODE_64;
#endif
}

static Property ppc_cpu_properties[] = {
    DEFINE_PROP_BOOL("pre-2.8-migration", PowerPCCPU, pre_2_8_migration, false),
    DEFINE_PROP_BOOL("pre-2.10-migration", PowerPCCPU, pre_2_10_migration,
                     false),
    DEFINE_PROP_BOOL("pre-3.0-migration", PowerPCCPU, pre_3_0_migration,
                     false),
    DEFINE_PROP_END_OF_LIST(),
};

#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"

static struct TCGCPUOps ppc_tcg_ops = {
  .initialize = ppc_translate_init,
  .cpu_exec_interrupt = ppc_cpu_exec_interrupt,
  .tlb_fill = ppc_cpu_tlb_fill,

#ifndef CONFIG_USER_ONLY
  .do_interrupt = ppc_cpu_do_interrupt,
  .cpu_exec_enter = ppc_cpu_exec_enter,
  .cpu_exec_exit = ppc_cpu_exec_exit,
  .do_unaligned_access = ppc_cpu_do_unaligned_access,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void ppc_cpu_class_init(ObjectClass *oc, void *data)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_parent_realize(dc, ppc_cpu_realize,
                                    &pcc->parent_realize);
    device_class_set_parent_unrealize(dc, ppc_cpu_unrealize,
                                      &pcc->parent_unrealize);
    pcc->pvr_match = ppc_pvr_match_default;
    pcc->interrupts_big_endian = ppc_cpu_interrupts_big_endian_always;
    device_class_set_props(dc, ppc_cpu_properties);

    device_class_set_parent_reset(dc, ppc_cpu_reset, &pcc->parent_reset);

    cc->class_by_name = ppc_cpu_class_by_name;
    cc->has_work = ppc_cpu_has_work;
    cc->dump_state = ppc_cpu_dump_state;
    cc->dump_statistics = ppc_cpu_dump_statistics;
    cc->set_pc = ppc_cpu_set_pc;
    cc->gdb_read_register = ppc_cpu_gdb_read_register;
    cc->gdb_write_register = ppc_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    cc->get_phys_page_debug = ppc_cpu_get_phys_page_debug;
    cc->vmsd = &vmstate_ppc_cpu;
#endif
#if defined(CONFIG_SOFTMMU)
    cc->write_elf64_note = ppc64_cpu_write_elf64_note;
    cc->write_elf32_note = ppc32_cpu_write_elf32_note;
#endif

    cc->gdb_num_core_regs = 71;
#ifndef CONFIG_USER_ONLY
    cc->gdb_get_dynamic_xml = ppc_gdb_get_dynamic_xml;
#endif
#ifdef USE_APPLE_GDB
    cc->gdb_read_register = ppc_cpu_gdb_read_register_apple;
    cc->gdb_write_register = ppc_cpu_gdb_write_register_apple;
    cc->gdb_num_core_regs = 71 + 32;
#endif

    cc->gdb_arch_name = ppc_gdb_arch_name;
#if defined(TARGET_PPC64)
    cc->gdb_core_xml_file = "power64-core.xml";
#else
    cc->gdb_core_xml_file = "power-core.xml";
#endif
#ifndef CONFIG_USER_ONLY
    cc->virtio_is_big_endian = ppc_cpu_is_big_endian;
#endif
    cc->disas_set_info = ppc_disas_set_info;

    dc->fw_name = "PowerPC,UNKNOWN";

#ifdef CONFIG_TCG
    cc->tcg_ops = &ppc_tcg_ops;
#endif /* CONFIG_TCG */
}

static void ppc_cpu_instance_init(Object *obj)
{
    PowerPCCPU *cpu = POWERPC_CPU(obj);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;

    cpu_set_cpustate_pointers(cpu);
    cpu->vcpu_id = UNASSIGNED_CPU_INDEX;

    env->msr_mask = pcc->msr_mask;
    env->mmu_model = pcc->mmu_model;
    env->excp_model = pcc->excp_model;
    env->bus_model = pcc->bus_model;
    env->insns_flags = pcc->insns_flags;
    env->insns_flags2 = pcc->insns_flags2;
    env->flags = pcc->flags;
    env->bfd_mach = pcc->bfd_mach;
    env->check_pow = pcc->check_pow;

    /*
     * Mark HV mode as supported if the CPU has an MSR_HV bit in the
     * msr_mask. The mask can later be cleared by PAPR mode but the hv
     * mode support will remain, thus enforcing that we cannot use
     * priv. instructions in guest in PAPR mode. For 970 we currently
     * simply don't set HV in msr_mask thus simulating an "Apple mode"
     * 970. If we ever want to support 970 HV mode, we'll have to add
     * a processor attribute of some sort.
     */
#if !defined(CONFIG_USER_ONLY)
    env->has_hv_mode = !!(env->msr_mask & MSR_HVB);
#endif

#ifdef CONFIG_TCG
    ppc_hash64_init(cpu);
#endif
}

static void ppc_cpu_instance_finalize(Object *obj)
{
#ifdef CONFIG_TCG
    PowerPCCPU *cpu = POWERPC_CPU(obj);

    ppc_hash64_finalize(cpu);
#endif
}

static const TypeInfo ppc_cpu_type_info = {
    .name = TYPE_POWERPC_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(PowerPCCPU),
    .instance_align = __alignof__(PowerPCCPU),
    .instance_init = ppc_cpu_instance_init,
    .instance_finalize = ppc_cpu_instance_finalize,
    .abstract = true,
    .class_size = sizeof(PowerPCCPUClass),
    .class_init = ppc_cpu_class_init,
};

static void ppc_cpu_register_types(void)
{
    type_register_static(&ppc_cpu_type_info);
#ifndef CONFIG_USER_ONLY
    type_register_static(&ppc_vhyp_type_info);
#endif
}

type_init(ppc_cpu_register_types)

static void ppc_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);
    DeviceClass *family = DEVICE_CLASS(ppc_cpu_get_family_class(pcc));
    const char *typename = object_class_get_name(oc);
    char *name;
    int i;

    if (unlikely(strcmp(typename, TYPE_HOST_POWERPC_CPU) == 0)) {
        return;
    }

    name = g_strndup(typename,
                     strlen(typename) - strlen(POWERPC_CPU_TYPE_SUFFIX));
    qemu_printf("PowerPC %-16s PVR %08x\n", name, pcc->pvr);
    for (i = 0; ppc_cpu_aliases[i].alias != NULL; i++) {
        PowerPCCPUAlias *alias = &ppc_cpu_aliases[i];
        ObjectClass *alias_oc = ppc_cpu_class_by_name(alias->model);

        if (alias_oc != oc) {
            continue;
        }
        /*
         * If running with KVM, we might update the family alias later, so
         * avoid printing the wrong alias here and use "preferred" instead
         */
        if (strcmp(alias->alias, family->desc) == 0) {
            qemu_printf("PowerPC %-16s (alias for preferred %s CPU)\n",
                        alias->alias, family->desc);
        } else {
            qemu_printf("PowerPC %-16s (alias for %s)\n",
                        alias->alias, name);
        }
    }
    g_free(name);
}

void ppc_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list(TYPE_POWERPC_CPU, false);
    list = g_slist_sort(list, ppc_cpu_list_compare);
    g_slist_foreach(list, ppc_cpu_list_entry, NULL);
    g_slist_free(list);

#ifdef CONFIG_KVM
    qemu_printf("\n");
    qemu_printf("PowerPC %-16s\n", "host");
#endif
}

static void ppc_cpu_defs_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **first = user_data;
    const char *typename;
    CpuDefinitionInfo *info;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = g_strndup(typename,
                           strlen(typename) - strlen(POWERPC_CPU_TYPE_SUFFIX));

    QAPI_LIST_PREPEND(*first, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;
    int i;

    list = object_class_get_list(TYPE_POWERPC_CPU, false);
    g_slist_foreach(list, ppc_cpu_defs_entry, &cpu_list);
    g_slist_free(list);

    for (i = 0; ppc_cpu_aliases[i].alias != NULL; i++) {
        PowerPCCPUAlias *alias = &ppc_cpu_aliases[i];
        ObjectClass *oc;
        CpuDefinitionInfo *info;

        oc = ppc_cpu_class_by_name(alias->model);
        if (oc == NULL) {
            continue;
        }

        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(alias->alias);
        info->q_typename = g_strdup(object_class_get_name(oc));

        QAPI_LIST_PREPEND(cpu_list, info);
    }

    return cpu_list;
}
#if !defined(CONFIG_USER_ONLY)
void cpu_ppc_set_vhyp(PowerPCCPU *cpu, PPCVirtualHypervisor *vhyp)
{
    CPUPPCState *env = &cpu->env;

    cpu->vhyp = vhyp;

    /*
     * With a virtual hypervisor mode we never allow the CPU to go
     * hypervisor mode itself
     */
    env->msr_mask &= ~MSR_HVB;
}

#endif /* !defined(CONFIG_USER_ONLY) */
