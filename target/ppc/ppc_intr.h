#ifndef PPC_INTR_H
#define PPC_INTR_H

typedef struct PPCIntrArgs PPCIntrArgs;
typedef struct PPCInterrupt PPCInterrupt;
typedef void (*ppc_intr_fn_t)(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);

struct PPCIntrArgs {
    target_ulong nip;
    target_ulong msr;
    target_ulong new_nip;
    target_ulong new_msr;
    int sprn_srr0;
    int sprn_srr1;
};

struct PPCInterrupt {
    const char *name;
    ppc_intr_fn_t fn;
};

void ppc_intr_alignment(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_critical(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_dabr(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_data_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_debug(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_embedded_doorbell_crit(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_embedded_fp_data(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_embedded_fp_round(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_embedded_perf_monitor(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_emulation(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_external(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_facility_unavail(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_fit(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_fpa(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_data_segment(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_data_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_decrementer(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_doorbell(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_emulation(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_facility_unavail(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_insn_segment(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_insn_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_virtualization(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_iabr(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_insn_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_io_error(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_machine_check(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_maint(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_mextbr(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_nmextbr(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_noop(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_perfm(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_program(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_programmable_timer(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_run_mode(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_smi(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_softp(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_spe_unavailable(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_system_call(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_system_call_vectored(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_system_reset(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_therm(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_tlb_miss(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_vpua(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_watchdog(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);

extern PPCInterrupt interrupts[POWERPC_EXCP_NB];

#endif /* PPC_INTR_H */
