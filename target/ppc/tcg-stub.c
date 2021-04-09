#include "qemu/osdep.h"
#include "cpu.h"
#include "mmu-hash64.h"

/* STUFF FOR FIRST LINKER ERROR */
/* This stuff happens in target/ppc files */

#if !defined(CONFIG_USER_ONLY)

void ppc_store_sdr1(CPUPPCState *env, target_ulong value) {
    /* stub to make things compile */
    return;
}

void ppc_store_ptcr(CPUPPCState *env, target_ulong value) {
    /* stub to make things compile */
    return;
}

#endif /* !defined(CONFIG_USER_ONLY) */
void ppc_store_msr(CPUPPCState *env, target_ulong value) {
    /* stub to make things compile */
    return;
}

void dump_mmu(CPUPPCState *env){
    /* stub to make things compile */
    return;
}

void store_fpscr(CPUPPCState *env, uint64_t arg, uint32_t mask) {
    /* stub to make things compile */
    return;
}

void ppc_cpu_do_interrupt(CPUState *cpu) {
    /* stub to make things compile */
    return;
}

/* STUFF FOR SECOND LINKER ERROR*/
/* these errors happen mostly in hw/ppc */

#ifdef TARGET_PPC64
int ppc_store_slb(PowerPCCPU *cpu, target_ulong slot,
                  target_ulong esid, target_ulong vsid) {
    /* rquired by kvm.c and machine.c */
    return 0;
}

void ppc_hash64_filter_pagesizes(PowerPCCPU *cpu,
                                 bool (*cb)(void *, uint32_t, uint32_t),
                                 void *opaque) {
    /* required by spapr_caps.c */
    return; 
}

void ppc_store_lpcr(PowerPCCPU *cpu, target_ulong val) {
    /* required by spapr_* */
    return;
}

const ppc_hash_pte64_t *ppc_hash64_map_hptes(PowerPCCPU *cpu,
                                             hwaddr ptex, int n) {
    /* used by spapr_hcall a bunch */
    return NULL;
}

void ppc_hash64_unmap_hptes(PowerPCCPU *cpu, const ppc_hash_pte64_t *hptes,
                            hwaddr ptex, int n) {
    /* used a bunch by spapr_hcall */
    return; 
}

void ppc_hash64_tlb_flush_hpte(PowerPCCPU *cpu,
                               target_ulong pte_index,
                               target_ulong pte0, target_ulong pte1){
    return; 
}

unsigned ppc_hash64_hpte_page_shift_noslb(PowerPCCPU *cpu,
                                          uint64_t pte0, uint64_t pte1) {
    return 0;
}
#endif

void ppc_cpu_do_fwnmi_machine_check(CPUState *cs, target_ulong vector) {
    /* required by spapr_events spapr_mce_dispatch_elog */
    return;
}
#ifndef CONFIG_USER_ONLY
void ppc_cpu_do_system_reset(CPUState *cs){
    /* required by pnv and spapr */
    return;
}
#endif

bool ppc64_v3_get_pate(PowerPCCPU *cpu, target_ulong lpid,
                       ppc_v3_pate_t *entry);

bool ppc64_v3_get_pate(PowerPCCPU *cpu, target_ulong lpid,
                       ppc_v3_pate_t *entry) {
    /* used by spapr_hcall: ppc_hash64_hpt_mask */
    return true;
}

/* THIRD BATCH OF ERRORS, AFTER MOVING STUFF FROM TRANSLATE TO CPU.C */

/* they are all coming from cpu.c, probably */

void create_ppc_opcodes(PowerPCCPU *cpu, Error **errp) {
    return;
}

void init_ppc_proc(PowerPCCPU *cpu) {
    return;
}

void destroy_ppc_opcodes(PowerPCCPU *cpu) {
    return;
}

void ppc_tlb_invalidate_all(CPUPPCState *env) {
    return;
}

void ppc_cpu_dump_state(CPUState *cpu, FILE *f, int flags) {
    return;
}

void ppc_cpu_dump_statistics(CPUState *cpu, int flags) {
    return;
}

#include "exec/hwaddr.h"

hwaddr ppc_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr) {
    return 0;
}
