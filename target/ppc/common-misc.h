#ifndef COMMON_MISC_H
#define COMMON_MISC_H
#include "qemu/osdep.h"
#include "cpu.h"

void ppc_store_vscr(CPUPPCState *env, uint64_t vscr);
uint32_t ppc_load_vscr(CPUPPCState *env);
void ppc_store_lpcr(PowerPCCPU *cpu, target_ulong val);
void ppc_hash64_filter_pagesizes(PowerPCCPU *cpu,
                                 bool (*cb)(void *, uint32_t, uint32_t),
                                 void *opaque);

#endif
