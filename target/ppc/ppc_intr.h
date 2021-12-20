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
void ppc_intr_data_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_debug(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_embedded_doorbell_crit(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_external(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_facility_unavail(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_fit(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_facility_unavail(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_hv_insn_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_insn_storage(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_machine_check(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_noop(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_program(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_programmable_timer(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_spe_unavailable(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_system_call(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_system_call_vectored(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_system_reset(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_tlb_miss(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);
void ppc_intr_watchdog(PowerPCCPU *cpu, PPCIntrArgs *regs, bool *ignore);

extern PPCInterrupt interrupts[POWERPC_EXCP_NB];

#endif /* PPC_INTR_H */
