/*
 * CPU TLB Helpers
 */

#ifndef CPUTLB_H
#define CPUTBL_H

uint64_t io_readx(CPUArchState *env, CPUIOTLBEntry *iotlbentry,
                  int mmu_idx,
                  target_ulong addr, uintptr_t retaddr,
                  bool recheck, MMUAccessType access_type, int size);

void io_writex(CPUArchState *env, CPUIOTLBEntry *iotlbentry,
               int mmu_idx,
               uint64_t val, target_ulong addr,
               uintptr_t retaddr, bool recheck, int size);

bool victim_tlb_hit(CPUArchState *env, size_t mmu_idx, size_t index,
                    size_t elt_ofs, target_ulong page);

#endif
