/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synopsys Inc.
 * Contributed by Cupertino Miranda <cmiranda@synopsys.com>
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
#include "qapi/error.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "migration/vmstate.h"
#include "exec/log.h"
#include "mmu.h"
#include "mpu.h"
#include "hw/qdev-properties.h"
#include "irq.h"
#include "hw/arc/cpudevs.h"
#include "timer.h"
#include "internals.h"

static const VMStateDescription vms_arc_cpu = {
    .name               = "cpu",
    .version_id         = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
      VMSTATE_END_OF_LIST()
    }
};

static Property arc_cpu_properties[] = {
    DEFINE_PROP_UINT32("address-size", ARCCPU, cfg.addr_size, 32),
    DEFINE_PROP_BOOL("aps", ARCCPU, cfg.aps_feature, false),
    DEFINE_PROP_BOOL("byte-order", ARCCPU, cfg.byte_order, false),
    DEFINE_PROP_BOOL("bitscan", ARCCPU, cfg.bitscan_option, true),
    DEFINE_PROP_UINT32("br_bc-entries", ARCCPU, cfg.br_bc_entries, -1),
    DEFINE_PROP_UINT32("br_pt-entries", ARCCPU, cfg.br_pt_entries, -1),
    DEFINE_PROP_BOOL("full-tag", ARCCPU, cfg.br_bc_full_tag, false),
    DEFINE_PROP_UINT8("rs-entries", ARCCPU, cfg.br_rs_entries, -1),
    DEFINE_PROP_UINT32("tag-size", ARCCPU, cfg.br_bc_tag_size, -1),
    DEFINE_PROP_UINT8("tosq-entries", ARCCPU, cfg.br_tosq_entries, -1),
    DEFINE_PROP_UINT8("fb-entries", ARCCPU, cfg.br_fb_entries, -1),
    DEFINE_PROP_BOOL("code-density", ARCCPU, cfg.code_density, true),
    DEFINE_PROP_BOOL("code-protect", ARCCPU, cfg.code_protect, false),
    DEFINE_PROP_UINT8("dcc-memcyc", ARCCPU, cfg.dccm_mem_cycles, -1),
    DEFINE_PROP_BOOL("ddcm-posedge", ARCCPU, cfg.dccm_posedge, false),
    DEFINE_PROP_UINT8("dcc-mem-banks", ARCCPU, cfg.dccm_mem_bancks, -1),
    DEFINE_PROP_UINT8("mem-cycles", ARCCPU, cfg.dc_mem_cycles, -1),
    DEFINE_PROP_BOOL("dc-posedge", ARCCPU, cfg.dc_posedge, false),
    DEFINE_PROP_BOOL("unaligned", ARCCPU, cfg.dmp_unaligned, true),
    DEFINE_PROP_BOOL("ecc-excp", ARCCPU, cfg.ecc_exception, false),
    DEFINE_PROP_UINT32("ext-irq", ARCCPU, cfg.external_interrupts, 128),
    DEFINE_PROP_UINT8("ecc-option", ARCCPU, cfg.ecc_option, -1),
    DEFINE_PROP_BOOL("firq", ARCCPU, cfg.firq_option, true),
    DEFINE_PROP_BOOL("fpu-dp", ARCCPU, cfg.fpu_dp_option, false),
    DEFINE_PROP_BOOL("fpu-fma", ARCCPU, cfg.fpu_fma_option, false),
    DEFINE_PROP_BOOL("fpu-div", ARCCPU, cfg.fpu_div_option, false),
    DEFINE_PROP_BOOL("actionpoints", ARCCPU, cfg.has_actionpoints, false),
    DEFINE_PROP_BOOL("fpu", ARCCPU, cfg.has_fpu, false),
    DEFINE_PROP_BOOL("has-irq", ARCCPU, cfg.has_interrupts, true),
    DEFINE_PROP_BOOL("has-mmu", ARCCPU, cfg.has_mmu, true),
    DEFINE_PROP_BOOL("has-mpu", ARCCPU, cfg.has_mpu, true),
    DEFINE_PROP_BOOL("timer0", ARCCPU, cfg.has_timer_0, true),
    DEFINE_PROP_BOOL("timer1", ARCCPU, cfg.has_timer_1, true),
    DEFINE_PROP_BOOL("has-pct", ARCCPU, cfg.has_pct, false),
    DEFINE_PROP_BOOL("has-rtt", ARCCPU, cfg.has_rtt, false),
    DEFINE_PROP_BOOL("has-smart", ARCCPU, cfg.has_smart, false),
    DEFINE_PROP_UINT32("intv-base", ARCCPU, cfg.intvbase_preset, 0x0),
    DEFINE_PROP_UINT32("lpc-size", ARCCPU, cfg.lpc_size, 32),
    DEFINE_PROP_UINT8("mpu-numreg", ARCCPU, cfg.mpu_num_regions, 0),
    DEFINE_PROP_UINT8("mpy-option", ARCCPU, cfg.mpy_option, 2),
    DEFINE_PROP_UINT32("mmu-pagesize0", ARCCPU, cfg.mmu_page_size_sel0, -1),
    DEFINE_PROP_UINT32("mmu-pagesize1", ARCCPU, cfg.mmu_page_size_sel1, -1),
    DEFINE_PROP_UINT32("mmu-pae", ARCCPU, cfg.mmu_pae_enabled, -1),
    DEFINE_PROP_UINT32("ntlb-numentries", ARCCPU, cfg.ntlb_num_entries, -1),
    DEFINE_PROP_UINT32("num-actionpoints", ARCCPU, cfg.num_actionpoints, -1),
    DEFINE_PROP_UINT32("num-irq", ARCCPU, cfg.number_of_interrupts, 240),
    DEFINE_PROP_UINT32("num-irqlevels", ARCCPU, cfg.number_of_levels, 15),
    DEFINE_PROP_UINT32("pct-counters", ARCCPU, cfg.pct_counters, -1),
    DEFINE_PROP_UINT32("pct-irq", ARCCPU, cfg.pct_interrupt, -1),
    DEFINE_PROP_UINT32("pc-size", ARCCPU, cfg.pc_size, 32),
    DEFINE_PROP_UINT32("num-regs", ARCCPU, cfg.rgf_num_regs, 32),
    DEFINE_PROP_UINT32("banked-regs", ARCCPU, cfg.rgf_banked_regs, -1),
    DEFINE_PROP_UINT32("num-banks", ARCCPU, cfg.rgf_num_banks, 0),
    DEFINE_PROP_BOOL("rtc-opt", ARCCPU, cfg.rtc_option, false),
    DEFINE_PROP_UINT32("rtt-featurelevel", ARCCPU, cfg.rtt_feature_level, -1),
    DEFINE_PROP_BOOL("stack-check", ARCCPU, cfg.stack_checking, false),
    DEFINE_PROP_BOOL("swap-option", ARCCPU, cfg.swap_option, true),
    DEFINE_PROP_UINT32("smrt-stackentries", ARCCPU, cfg.smar_stack_entries, -1),
    DEFINE_PROP_UINT32("smrt-impl", ARCCPU, cfg.smart_implementation, -1),
    DEFINE_PROP_UINT32("stlb", ARCCPU, cfg.stlb_num_entries, -1),
    DEFINE_PROP_UINT32("slc-size", ARCCPU, cfg.slc_size, -1),
    DEFINE_PROP_UINT32("slc-linesize", ARCCPU, cfg.slc_line_size, -1),
    DEFINE_PROP_UINT32("slc-ways", ARCCPU, cfg.slc_ways, -1),
    DEFINE_PROP_UINT32("slc-tagbanks", ARCCPU, cfg.slc_tag_banks, -1),
    DEFINE_PROP_UINT32("slc-tram", ARCCPU, cfg.slc_tram_delay, -1),
    DEFINE_PROP_UINT32("slc-dbank", ARCCPU, cfg.slc_dbank_width, -1),
    DEFINE_PROP_UINT32("slc-data", ARCCPU, cfg.slc_data_banks, -1),
    DEFINE_PROP_UINT32("slc-delay", ARCCPU, cfg.slc_dram_delay, -1),
    DEFINE_PROP_BOOL("slc-memwidth", ARCCPU, cfg.slc_mem_bus_width, false),
    DEFINE_PROP_UINT32("slc-ecc", ARCCPU, cfg.slc_ecc_option, -1),
    DEFINE_PROP_BOOL("slc-datahalf", ARCCPU, cfg.slc_data_halfcycle_steal, false),
    DEFINE_PROP_BOOL("slc-dataadd", ARCCPU, cfg.slc_data_add_pre_pipeline, false),
    DEFINE_PROP_BOOL("uaux", ARCCPU, cfg.uaux_option, false),
    DEFINE_PROP_UINT32("freq_hz", ARCCPU, cfg.freq_hz, 4600000),
    DEFINE_PROP_END_OF_LIST(),
};

static void arc_cpu_set_pc(CPUState *cs, vaddr value)
{
  ARCCPU *cpu = ARC_CPU(cs);

  CPU_PCL(&cpu->env) = value & 0xfffffffc;
  cpu->env.pc = value;
}

static bool arc_cpu_has_work(CPUState *cs)
{
  return cs->interrupt_request & CPU_INTERRUPT_HARD;
}

static void arc_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
  ARCCPU      *cpu = ARC_CPU(cs);
  CPUARCState *env = &cpu->env;

  CPU_PCL(&cpu->env) = tb->pc & 0xfffffffc;
  env->pc = tb->pc;
}

static void arc_cpu_reset(DeviceState *dev)
{
  CPUState *s = CPU(dev);
  ARCCPU *cpu = ARC_CPU(s);
  ARCCPUClass *arcc = ARC_CPU_GET_CLASS(cpu);
  CPUARCState *env = &cpu->env;

  if (qemu_loglevel_mask(CPU_LOG_RESET)) {
    qemu_log("CPU Reset (CPU)\n");
    log_cpu_state(s, 0);
  }

  /* Initialize mmu/reset it. */
  arc_mmu_init(&env->mmu);

  arc_mpu_init(cpu);

  arc_resetTIMER(cpu);
  arc_resetIRQ(cpu);

  arcc->parent_reset(dev);

  memset(env->r, 0, sizeof(env->r));
  env->lock_lf_var = 0;

  env->stat.is_delay_slot_instruction = 0;
    /*
     * kernel expects MPY support to check for presence of
     * extension core regs r58/r59.
     *
     * VERSION32x32=0x06: ARCv2 32x32 Multiply
     * DSP=0x1: MPY_OPTION 7
     */
    env->mpy_build = 0x00001006;
}

static void arc_cpu_disas_set_info(CPUState *cs, disassemble_info *info)
{
  ARCCPU *cpu = ARC_CPU(cs);
  CPUARCState *env = &cpu->env;

  switch (env->family) {
  case ARC_OPCODE_ARC700:
    info->mach = bfd_mach_arc_arc700;
    break;
  case ARC_OPCODE_ARC600:
    info->mach = bfd_mach_arc_arc600;
    break;
  case ARC_OPCODE_ARCv2EM:
    info->mach = bfd_mach_arc_arcv2em;
    break;
  case ARC_OPCODE_ARCv2HS:
    info->mach = bfd_mach_arc_arcv2hs;
    break;
  default:
    info->mach = bfd_mach_arc_arcv2;
    break;
  }

  info->print_insn = print_insn_arc;
  info->endian = BFD_ENDIAN_LITTLE;
}


static void arc_cpu_realizefn(DeviceState *dev, Error **errp)
{
  CPUState *cs = CPU(dev);
  ARCCPU *cpu = ARC_CPU(dev);
  ARCCPUClass *arcc = ARC_CPU_GET_CLASS(dev);
  Error *local_err = NULL;
  CPUARCState *env = &cpu->env;

  cpu_exec_realizefn(cs, &local_err);
  if (local_err != NULL) {
    error_propagate(errp, local_err);
    return;
  }

  arc_cpu_register_gdb_regs_for_features(cpu);

  qemu_init_vcpu(cs);

  /*
   * Initialize build registers depending on the simulation
   * parameters.
   */
  env->freq_hz = cpu->cfg.freq_hz;

  env->isa_config = 0x02;
  switch (cpu->cfg.pc_size) {
  case 16:
    break;
  case 20:
    env->isa_config |= 1 << 8;
    break;
  case 24:
    env->isa_config |= 2 << 8;
    break;
  case 28:
    env->isa_config |= 3 << 8;
    break;
  default:
    env->isa_config |= 4 << 8;
    break;
  }

  switch (cpu->cfg.lpc_size) {
  case 0:
    break;
  case 8:
    env->isa_config |= 1 << 12;
    break;
  case 12:
    env->isa_config |= 2 << 12;
    break;
  case 16:
    env->isa_config |= 3 << 12;
    break;
  case 20:
    env->isa_config |= 4 << 12;
    break;
  case 24:
    env->isa_config |= 5 << 12;
    break;
  case 28:
    env->isa_config |= 6 << 12;
    break;
  default:
    env->isa_config |= 7 << 12;
    break;
  }

  switch (cpu->cfg.addr_size) {
  case 16:
    break;
  case 20:
    env->isa_config |= 1 << 16;
    break;
  case 24:
    env->isa_config |= 2 << 16;
    break;
  case 28:
    env->isa_config |= 3 << 16;
    break;
  default:
    env->isa_config |= 4 << 16;
    break;
  }

  env->isa_config |= (cpu->cfg.byte_order ? BIT(20) : 0) | BIT(21)
    | (cpu->cfg.dmp_unaligned ? BIT(22) : 0) | BIT(23)
    | (cpu->cfg.code_density ? (2 << 24) : 0) | BIT(28);

  arc_initializeTIMER(cpu);
  arc_initializeIRQ(cpu);

  cpu_reset(cs);

  arcc->parent_realize(dev, errp);
}

static void arc_cpu_initfn(Object *obj)
{
  ARCCPU *cpu = ARC_CPU(obj);

  /* Initialize aux-regs. */
  arc_aux_regs_init();

  cpu_set_cpustate_pointers(cpu);
}

static ObjectClass *arc_cpu_class_by_name(const char *cpu_model)
{
  ObjectClass *oc;
  char *typename;
  char **cpuname;

  if (!cpu_model) {
    return NULL;
  }

  cpuname = g_strsplit(cpu_model, ",", 1);
  typename = g_strdup_printf("%s-" TYPE_ARC_CPU, cpuname[0]);
  oc = object_class_by_name(typename);

  g_strfreev(cpuname);
  g_free(typename);

  if (!oc
      || !object_class_dynamic_cast(oc, TYPE_ARC_CPU)
      || object_class_is_abstract(oc)) {
    return NULL;
  }

  return oc;
}

static gchar *arc_gdb_arch_name(CPUState *cs)
{
    return g_strdup("arc:ARCv2");
}

static void arc_cpu_class_init(ObjectClass *oc, void *data)
{
  DeviceClass *dc = DEVICE_CLASS(oc);
  CPUClass *cc = CPU_CLASS(oc);
  ARCCPUClass *arcc = ARC_CPU_CLASS(oc);

  device_class_set_parent_realize(dc, arc_cpu_realizefn,
                                  &arcc->parent_realize);

  device_class_set_parent_reset(dc, arc_cpu_reset, &arcc->parent_reset);

  cc->class_by_name = arc_cpu_class_by_name;

  cc->has_work = arc_cpu_has_work;
  cc->do_interrupt = arc_cpu_do_interrupt;
  cc->cpu_exec_interrupt = arc_cpu_exec_interrupt;
  cc->dump_state = arc_cpu_dump_state;
  cc->set_pc = arc_cpu_set_pc;
#ifndef CONFIG_USER_ONLY
  cc->memory_rw_debug = arc_cpu_memory_rw_debug;
  cc->get_phys_page_debug = arc_cpu_get_phys_page_debug;
  cc->vmsd = &vms_arc_cpu;
#endif
  cc->disas_set_info = arc_cpu_disas_set_info;
  cc->synchronize_from_tb = arc_cpu_synchronize_from_tb;
  cc->gdb_read_register = arc_cpu_gdb_read_register;
  cc->gdb_write_register = arc_cpu_gdb_write_register;

  /* Core GDB support */
  cc->gdb_core_xml_file = "arc-v2-core.xml";
  cc->gdb_num_core_regs = GDB_REG_LAST;
  cc->gdb_arch_name = arc_gdb_arch_name;

#ifdef CONFIG_TCG
    cc->tcg_initialize = arc_translate_init;
    cc->tlb_fill = arc_cpu_tlb_fill;
#endif
    device_class_set_props(dc, arc_cpu_properties);
}

static void arc_any_initfn(Object *obj)
{
  /* Set cpu feature flags */
  ARCCPU *cpu = ARC_CPU(obj);
  cpu->env.family = ARC_OPCODE_ARC700;
}

static void arc600_initfn(Object *obj)
{
  ARCCPU *cpu = ARC_CPU(obj);
  cpu->env.family = ARC_OPCODE_ARC600;
}

static void arc700_initfn(Object *obj)
{
  ARCCPU *cpu = ARC_CPU(obj);
  cpu->env.family = ARC_OPCODE_ARC700;
}

static void arcem_initfn(Object *obj)
{
  ARCCPU *cpu = ARC_CPU(obj);
  cpu->env.family = ARC_OPCODE_ARCv2EM;
}

static void archs_initfn(Object *obj)
{
  ARCCPU *cpu = ARC_CPU(obj);
  cpu->env.family = ARC_OPCODE_ARCv2HS;
}

typedef struct ARCCPUInfo {
  const char     *name;
  void (*initfn)(Object *obj);
} ARCCPUInfo;

static const ARCCPUInfo arc_cpus[] = {
  { .name = "arc600", .initfn = arc600_initfn },
  { .name = "arc700", .initfn = arc700_initfn },
  { .name = "arcem", .initfn = arcem_initfn },
  { .name = "archs", .initfn = archs_initfn },
  { .name = "any", .initfn = arc_any_initfn },
};

static void cpu_register(const ARCCPUInfo *info)
{
  TypeInfo type_info = {
    .parent = TYPE_ARC_CPU,
    .instance_size = sizeof(ARCCPU),
    .instance_init = info->initfn,
    .class_size = sizeof(ARCCPUClass),
  };

  type_info.name = g_strdup_printf("%s-" TYPE_ARC_CPU, info->name);
  type_register(&type_info);
  g_free((void *)type_info.name);
}

static const TypeInfo arc_cpu_type_info = {
  .name = TYPE_ARC_CPU,
  .parent = TYPE_CPU,
  .instance_size = sizeof(ARCCPU),
  .instance_init = arc_cpu_initfn,
  .class_size = sizeof(ARCCPUClass),
  .class_init = arc_cpu_class_init,
  .abstract = true,
};

static void arc_cpu_register_types(void)
{
    int i;
    type_register_static(&arc_cpu_type_info);

    for (i = 0; i < ARRAY_SIZE(arc_cpus); i++) {
        cpu_register(&arc_cpus[i]);
    }
}

type_init(arc_cpu_register_types)


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
