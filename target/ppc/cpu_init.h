#ifndef TARGET_PPC_CPU_INIT_H
#define TARGET_PPC_CPU_INIT_H

#define POWERPC_FAMILY_POWER9_INSNS_FLAGS                           \
    PPC_INSNS_BASE | PPC_ISEL | PPC_STRING | PPC_MFTB |             \
    PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |                   \
    PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE | PPC_FLOAT_FRSQRTES |      \
    PPC_FLOAT_STFIWX | PPC_FLOAT_EXT |PPC_CACHE | PPC_CACHE_ICBI |  \
    PPC_CACHE_DCBZ | PPC_MEM_SYNC | PPC_MEM_EIEIO | PPC_MEM_TLBIE | \
    PPC_MEM_TLBSYNC | PPC_64B | PPC_64H | PPC_64BX | PPC_ALTIVEC |  \
    PPC_SEGMENT_64B | PPC_SLBI | PPC_POPCNTB | PPC_POPCNTWD |       \
    PPC_CILDST

#define POWERPC_FAMILY_POWER9_INSNS_FLAGS2_COMMON                   \
    PPC2_VSX | PPC2_VSX207 | PPC2_DFP | PPC2_DBRX |                 \
    PPC2_PERM_ISA206 | PPC2_DIVE_ISA206 | PPC2_ATOMIC_ISA206 |      \
    PPC2_FP_CVT_ISA206 | PPC2_FP_TST_ISA206 | PPC2_BCTAR_ISA207 |   \
    PPC2_LSQ_ISA207 | PPC2_ALTIVEC_207 | PPC2_ISA205 |              \
    PPC2_ISA207S | PPC2_FP_CVT_S64 | PPC2_ISA300 | PPC2_PRCNTL |    \
    PPC2_MEM_LWSYNC | PPC2_BCDA_ISA206

#define POWERPC_FAMILY_POWER9_INSNS_FLAGS2                          \
    POWERPC_FAMILY_POWER9_INSNS_FLAGS2_COMMON | PPC2_TM
#define POWERPC_FAMILY_POWER10_INSNS_FLAGS2                         \
    POWERPC_FAMILY_POWER9_INSNS_FLAGS2_COMMON | PPC2_ISA310

#define POWERPC_POWER9_COMMON_PCC_MSR_MASK \
    (1ull << MSR_SF) |                     \
    (1ull << MSR_HV) |                     \
    (1ull << MSR_VR) |                     \
    (1ull << MSR_VSX) |                    \
    (1ull << MSR_EE) |                     \
    (1ull << MSR_PR) |                     \
    (1ull << MSR_FP) |                     \
    (1ull << MSR_ME) |                     \
    (1ull << MSR_FE0) |                    \
    (1ull << MSR_SE) |                     \
    (1ull << MSR_DE) |                     \
    (1ull << MSR_FE1) |                    \
    (1ull << MSR_IR) |                     \
    (1ull << MSR_DR) |                     \
    (1ull << MSR_PMM) |                    \
    (1ull << MSR_RI) |                     \
    (1ull << MSR_LE)

#define POWERPC_POWER9_PCC_MSR_MASK \
    POWERPC_POWER9_COMMON_PCC_MSR_MASK | (1ull << MSR_TM)
#define POWERPC_POWER10_PCC_MSR_MASK \
    POWERPC_POWER9_COMMON_PCC_MSR_MASK
#define POWERPC_POWER9_PCC_PCR_MASK \
    PCR_COMPAT_2_05 | PCR_COMPAT_2_06 | PCR_COMPAT_2_07
#define POWERPC_POWER10_PCC_PCR_MASK \
    POWERPC_POWER9_PCC_PCR_MASK | PCR_COMPAT_3_00
#define POWERPC_POWER9_PCC_PCR_SUPPORTED \
    PCR_COMPAT_3_00 | PCR_COMPAT_2_07 | PCR_COMPAT_2_06 | PCR_COMPAT_2_05
#define POWERPC_POWER10_PCC_PCR_SUPPORTED \
    POWERPC_POWER9_PCC_PCR_SUPPORTED | PCR_COMPAT_3_10
#define POWERPC_POWER9_PCC_LPCR_MASK                                        \
    LPCR_VPM1 | LPCR_ISL | LPCR_KBV | LPCR_DPFD |                           \
    (LPCR_PECE_U_MASK & LPCR_HVEE) | LPCR_ILE | LPCR_AIL |                  \
    LPCR_UPRT | LPCR_EVIRT | LPCR_ONL | LPCR_HR | LPCR_LD |                 \
    (LPCR_PECE_L_MASK & (LPCR_PDEE|LPCR_HDEE|LPCR_EEE|LPCR_DEE|LPCR_OEE)) | \
    LPCR_MER | LPCR_GTSE | LPCR_TC | LPCR_HEIC | LPCR_LPES0 | LPCR_HVICE |  \
    LPCR_HDICE
/* DD2 adds an extra HAIL bit */
#define POWERPC_POWER10_PCC_LPCR_MASK \
    POWERPC_POWER9_PCC_LPCR_MASK | LPCR_HAIL
#define POWERPC_POWER9_PCC_FLAGS_COMMON                                 \
    POWERPC_FLAG_VRE | POWERPC_FLAG_SE | POWERPC_FLAG_BE |              \
    POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK | POWERPC_FLAG_CFAR |       \
    POWERPC_FLAG_VSX | POWERPC_FLAG_SCV

#define POWERPC_POWER9_PCC_FLAGS  \
    POWERPC_POWER9_PCC_FLAGS_COMMON | POWERPC_FLAG_TM
#define POWERPC_POWER10_PCC_FLAGS \
    POWERPC_POWER9_PCC_FLAGS_COMMON | POWERPC_FLAG_BHRB

#endif /* TARGET_PPC_CPU_INIT_H */
