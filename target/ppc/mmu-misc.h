#ifndef MMU_MISC_H
#define MMU_MISC_H
#include "qemu/osdep.h"
#include "cpu.h"

#ifndef CONFIG_USER_ONLY

#ifdef TARGET_PPC64

void ppc_store_lpcr(PowerPCCPU *cpu, target_ulong val);
void ppc_hash64_filter_pagesizes(PowerPCCPU *cpu,
                                 bool (*cb)(void *, uint32_t, uint32_t),
                                 void *opaque);

#endif

void ppc_hash64_unmap_hptes(PowerPCCPU *cpu, const ppc_hash_pte64_t *hptes,
                            hwaddr ptex, int n);

#endif

#endif
