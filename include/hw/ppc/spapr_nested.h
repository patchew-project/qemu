#ifndef HW_SPAPR_NESTED_H
#define HW_SPAPR_NESTED_H

#include "qemu/osdep.h"
#include "target/ppc/cpu.h"

/* Guest State Buffer Element IDs */
#define GSB_HV_VCPU_IGNORED_ID  0x0000 /* An element whose value is ignored */
#define GSB_HV_VCPU_STATE_SIZE  0x0001 /* HV internal format VCPU state size */
#define GSB_VCPU_OUT_BUF_MIN_SZ 0x0002 /* Min size of the Run VCPU o/p buffer */
#define GSB_VCPU_LPVR           0x0003 /* Logical PVR */
#define GSB_TB_OFFSET           0x0004 /* Timebase Offset */
#define GSB_PART_SCOPED_PAGETBL 0x0005 /* Partition Scoped Page Table */
#define GSB_PROCESS_TBL         0x0006 /* Process Table */
                    /* RESERVED 0x0007 - 0x0BFF */
#define GSB_VCPU_IN_BUFFER      0x0C00 /* Run VCPU Input Buffer */
#define GSB_VCPU_OUT_BUFFER     0x0C01 /* Run VCPU Out Buffer */
#define GSB_VCPU_VPA            0x0C02 /* HRA to Guest VCPU VPA */
                    /* RESERVED 0x0C03 - 0x0FFF */
#define GSB_VCPU_GPR0           0x1000
#define GSB_VCPU_GPR1           0x1001
#define GSB_VCPU_GPR2           0x1002
#define GSB_VCPU_GPR3           0x1003
#define GSB_VCPU_GPR4           0x1004
#define GSB_VCPU_GPR5           0x1005
#define GSB_VCPU_GPR6           0x1006
#define GSB_VCPU_GPR7           0x1007
#define GSB_VCPU_GPR8           0x1008
#define GSB_VCPU_GPR9           0x1009
#define GSB_VCPU_GPR10          0x100A
#define GSB_VCPU_GPR11          0x100B
#define GSB_VCPU_GPR12          0x100C
#define GSB_VCPU_GPR13          0x100D
#define GSB_VCPU_GPR14          0x100E
#define GSB_VCPU_GPR15          0x100F
#define GSB_VCPU_GPR16          0x1010
#define GSB_VCPU_GPR17          0x1011
#define GSB_VCPU_GPR18          0x1012
#define GSB_VCPU_GPR19          0x1013
#define GSB_VCPU_GPR20          0x1014
#define GSB_VCPU_GPR21          0x1015
#define GSB_VCPU_GPR22          0x1016
#define GSB_VCPU_GPR23          0x1017
#define GSB_VCPU_GPR24          0x1018
#define GSB_VCPU_GPR25          0x1019
#define GSB_VCPU_GPR26          0x101A
#define GSB_VCPU_GPR27          0x101B
#define GSB_VCPU_GPR28          0x101C
#define GSB_VCPU_GPR29          0x101D
#define GSB_VCPU_GPR30          0x101E
#define GSB_VCPU_GPR31          0x101F
#define GSB_VCPU_HDEC_EXPIRY_TB 0x1020
#define GSB_VCPU_SPR_NIA        0x1021
#define GSB_VCPU_SPR_MSR        0x1022
#define GSB_VCPU_SPR_LR         0x1023
#define GSB_VCPU_SPR_XER        0x1024
#define GSB_VCPU_SPR_CTR        0x1025
#define GSB_VCPU_SPR_CFAR       0x1026
#define GSB_VCPU_SPR_SRR0       0x1027
#define GSB_VCPU_SPR_SRR1       0x1028
#define GSB_VCPU_SPR_DAR        0x1029
#define GSB_VCPU_DEC_EXPIRE_TB  0x102A
#define GSB_VCPU_SPR_VTB        0x102B
#define GSB_VCPU_SPR_LPCR       0x102C
#define GSB_VCPU_SPR_HFSCR      0x102D
#define GSB_VCPU_SPR_FSCR       0x102E
#define GSB_VCPU_SPR_FPSCR      0x102F
#define GSB_VCPU_SPR_DAWR0      0x1030
#define GSB_VCPU_SPR_DAWR1      0x1031
#define GSB_VCPU_SPR_CIABR      0x1032
#define GSB_VCPU_SPR_PURR       0x1033
#define GSB_VCPU_SPR_SPURR      0x1034
#define GSB_VCPU_SPR_IC         0x1035
#define GSB_VCPU_SPR_SPRG0      0x1036
#define GSB_VCPU_SPR_SPRG1      0x1037
#define GSB_VCPU_SPR_SPRG2      0x1038
#define GSB_VCPU_SPR_SPRG3      0x1039
#define GSB_VCPU_SPR_PPR        0x103A
#define GSB_VCPU_SPR_MMCR0      0x103B
#define GSB_VCPU_SPR_MMCR1      0x103C
#define GSB_VCPU_SPR_MMCR2      0x103D
#define GSB_VCPU_SPR_MMCR3      0x103E
#define GSB_VCPU_SPR_MMCRA      0x103F
#define GSB_VCPU_SPR_SIER       0x1040
#define GSB_VCPU_SPR_SIER2      0x1041
#define GSB_VCPU_SPR_SIER3      0x1042
#define GSB_VCPU_SPR_BESCR      0x1043
#define GSB_VCPU_SPR_EBBHR      0x1044
#define GSB_VCPU_SPR_EBBRR      0x1045
#define GSB_VCPU_SPR_AMR        0x1046
#define GSB_VCPU_SPR_IAMR       0x1047
#define GSB_VCPU_SPR_AMOR       0x1048
#define GSB_VCPU_SPR_UAMOR      0x1049
#define GSB_VCPU_SPR_SDAR       0x104A
#define GSB_VCPU_SPR_SIAR       0x104B
#define GSB_VCPU_SPR_DSCR       0x104C
#define GSB_VCPU_SPR_TAR        0x104D
#define GSB_VCPU_SPR_DEXCR      0x104E
#define GSB_VCPU_SPR_HDEXCR     0x104F
#define GSB_VCPU_SPR_HASHKEYR   0x1050
#define GSB_VCPU_SPR_HASHPKEYR  0x1051
#define GSB_VCPU_SPR_CTRL       0x1052
                    /* RESERVED 0x1053 - 0x1FFF */
#define GSB_VCPU_SPR_CR         0x2000
#define GSB_VCPU_SPR_PIDR       0x2001
#define GSB_VCPU_SPR_DSISR      0x2002
#define GSB_VCPU_SPR_VSCR       0x2003
#define GSB_VCPU_SPR_VRSAVE     0x2004
#define GSB_VCPU_SPR_DAWRX0     0x2005
#define GSB_VCPU_SPR_DAWRX1     0x2006
#define GSB_VCPU_SPR_PMC1       0x2007
#define GSB_VCPU_SPR_PMC2       0x2008
#define GSB_VCPU_SPR_PMC3       0x2009
#define GSB_VCPU_SPR_PMC4       0x200A
#define GSB_VCPU_SPR_PMC5       0x200B
#define GSB_VCPU_SPR_PMC6       0x200C
#define GSB_VCPU_SPR_WORT       0x200D
#define GSB_VCPU_SPR_PSPB       0x200E
                    /* RESERVED 0x200F - 0x2FFF */
#define GSB_VCPU_SPR_VSR0       0x3000
#define GSB_VCPU_SPR_VSR1       0x3001
#define GSB_VCPU_SPR_VSR2       0x3002
#define GSB_VCPU_SPR_VSR3       0x3003
#define GSB_VCPU_SPR_VSR4       0x3004
#define GSB_VCPU_SPR_VSR5       0x3005
#define GSB_VCPU_SPR_VSR6       0x3006
#define GSB_VCPU_SPR_VSR7       0x3007
#define GSB_VCPU_SPR_VSR8       0x3008
#define GSB_VCPU_SPR_VSR9       0x3009
#define GSB_VCPU_SPR_VSR10      0x300A
#define GSB_VCPU_SPR_VSR11      0x300B
#define GSB_VCPU_SPR_VSR12      0x300C
#define GSB_VCPU_SPR_VSR13      0x300D
#define GSB_VCPU_SPR_VSR14      0x300E
#define GSB_VCPU_SPR_VSR15      0x300F
#define GSB_VCPU_SPR_VSR16      0x3010
#define GSB_VCPU_SPR_VSR17      0x3011
#define GSB_VCPU_SPR_VSR18      0x3012
#define GSB_VCPU_SPR_VSR19      0x3013
#define GSB_VCPU_SPR_VSR20      0x3014
#define GSB_VCPU_SPR_VSR21      0x3015
#define GSB_VCPU_SPR_VSR22      0x3016
#define GSB_VCPU_SPR_VSR23      0x3017
#define GSB_VCPU_SPR_VSR24      0x3018
#define GSB_VCPU_SPR_VSR25      0x3019
#define GSB_VCPU_SPR_VSR26      0x301A
#define GSB_VCPU_SPR_VSR27      0x301B
#define GSB_VCPU_SPR_VSR28      0x301C
#define GSB_VCPU_SPR_VSR29      0x301D
#define GSB_VCPU_SPR_VSR30      0x301E
#define GSB_VCPU_SPR_VSR31      0x301F
#define GSB_VCPU_SPR_VSR32      0x3020
#define GSB_VCPU_SPR_VSR33      0x3021
#define GSB_VCPU_SPR_VSR34      0x3022
#define GSB_VCPU_SPR_VSR35      0x3023
#define GSB_VCPU_SPR_VSR36      0x3024
#define GSB_VCPU_SPR_VSR37      0x3025
#define GSB_VCPU_SPR_VSR38      0x3026
#define GSB_VCPU_SPR_VSR39      0x3027
#define GSB_VCPU_SPR_VSR40      0x3028
#define GSB_VCPU_SPR_VSR41      0x3029
#define GSB_VCPU_SPR_VSR42      0x302A
#define GSB_VCPU_SPR_VSR43      0x302B
#define GSB_VCPU_SPR_VSR44      0x302C
#define GSB_VCPU_SPR_VSR45      0x302D
#define GSB_VCPU_SPR_VSR46      0x302E
#define GSB_VCPU_SPR_VSR47      0x302F
#define GSB_VCPU_SPR_VSR48      0x3030
#define GSB_VCPU_SPR_VSR49      0x3031
#define GSB_VCPU_SPR_VSR50      0x3032
#define GSB_VCPU_SPR_VSR51      0x3033
#define GSB_VCPU_SPR_VSR52      0x3034
#define GSB_VCPU_SPR_VSR53      0x3035
#define GSB_VCPU_SPR_VSR54      0x3036
#define GSB_VCPU_SPR_VSR55      0x3037
#define GSB_VCPU_SPR_VSR56      0x3038
#define GSB_VCPU_SPR_VSR57      0x3039
#define GSB_VCPU_SPR_VSR58      0x303A
#define GSB_VCPU_SPR_VSR59      0x303B
#define GSB_VCPU_SPR_VSR60      0x303C
#define GSB_VCPU_SPR_VSR61      0x303D
#define GSB_VCPU_SPR_VSR62      0x303E
#define GSB_VCPU_SPR_VSR63      0x303F
                    /* RESERVED 0x3040 - 0xEFFF */
#define GSB_VCPU_SPR_HDAR       0xF000
#define GSB_VCPU_SPR_HDSISR     0xF001
#define GSB_VCPU_SPR_HEIR       0xF002
#define GSB_VCPU_SPR_ASDR       0xF003
/* End of list of Guest State Buffer Element IDs */
#define GSB_LAST                GSB_VCPU_SPR_ASDR

typedef struct SpaprMachineStateNestedGuest {
    unsigned long vcpus;
    struct SpaprMachineStateNestedGuestVcpu *vcpu;
    uint64_t parttbl[2];
    uint32_t pvr_logical;
    uint64_t tb_offset;
} SpaprMachineStateNestedGuest;

struct SpaprMachineStateNested {

    uint8_t api;
#define NESTED_API_KVM_HV  1
#define NESTED_API_PAPR    2
    uint64_t ptcr;
    uint32_t lpid_max;
    uint32_t pvr_base;
    bool capabilities_set;
    GHashTable *guests;
};

struct SpaprMachineStateNestedGuestVcpuRunBuf {
    uint64_t addr;
    uint64_t size;
};

typedef struct SpaprMachineStateNestedGuestVcpu {
    bool enabled;
    struct SpaprMachineStateNestedGuestVcpuRunBuf runbufin;
    struct SpaprMachineStateNestedGuestVcpuRunBuf runbufout;
    CPUPPCState env;
    int64_t tb_offset;
    int64_t dec_expiry_tb;
} SpaprMachineStateNestedGuestVcpu;

/*
 * Register state for entering a nested guest with H_ENTER_NESTED.
 * New member must be added at the end.
 */
struct kvmppc_hv_guest_state {
    uint64_t version;      /* version of this structure layout, must be first */
    uint32_t lpid;
    uint32_t vcpu_token;
    /* These registers are hypervisor privileged (at least for writing) */
    uint64_t lpcr;
    uint64_t pcr;
    uint64_t amor;
    uint64_t dpdes;
    uint64_t hfscr;
    int64_t tb_offset;
    uint64_t dawr0;
    uint64_t dawrx0;
    uint64_t ciabr;
    uint64_t hdec_expiry;
    uint64_t purr;
    uint64_t spurr;
    uint64_t ic;
    uint64_t vtb;
    uint64_t hdar;
    uint64_t hdsisr;
    uint64_t heir;
    uint64_t asdr;
    /* These are OS privileged but need to be set late in guest entry */
    uint64_t srr0;
    uint64_t srr1;
    uint64_t sprg[4];
    uint64_t pidr;
    uint64_t cfar;
    uint64_t ppr;
    /* Version 1 ends here */
    uint64_t dawr1;
    uint64_t dawrx1;
    /* Version 2 ends here */
    uint64_t dec;
    uint64_t fscr;
    uint64_t fpscr;
    uint64_t bescr;
    uint64_t ebbhr;
    uint64_t ebbrr;
    uint64_t tar;
    uint64_t dexcr;
    uint64_t hdexcr;
    uint64_t hashkeyr;
    uint64_t hashpkeyr;
    uint64_t ctrl;
    uint64_t vscr;
    uint64_t vrsave;
    ppc_vsr_t vsr[64];
};

/* Latest version of hv_guest_state structure */
#define HV_GUEST_STATE_VERSION  2

/* Linux 64-bit powerpc pt_regs struct, used by nested HV */
struct kvmppc_pt_regs {
    uint64_t gpr[32];
    uint64_t nip;
    uint64_t msr;
    uint64_t orig_gpr3;    /* Used for restarting system calls */
    uint64_t ctr;
    uint64_t link;
    uint64_t xer;
    uint64_t ccr;
    uint64_t softe;        /* Soft enabled/disabled */
    uint64_t trap;         /* Reason for being here */
    uint64_t dar;          /* Fault registers */
    uint64_t dsisr;        /* on 4xx/Book-E used for ESR */
    uint64_t result;       /* Result of a system call */
};

/*
 * nested_ppc_state is used to save the host CPU state before switching it to
 * the guest CPU state, to be restored on H_ENTER_NESTED exit.
 */
struct nested_ppc_state {
    uint64_t gpr[32];
    uint64_t lr;
    uint64_t ctr;
    uint64_t cfar;
    uint64_t msr;
    uint64_t nip;
    uint32_t cr;

    uint64_t xer;

    uint64_t lpcr;
    uint64_t lpidr;
    uint64_t pidr;
    uint64_t pcr;
    uint64_t dpdes;
    uint64_t hfscr;
    uint64_t srr0;
    uint64_t srr1;
    uint64_t sprg0;
    uint64_t sprg1;
    uint64_t sprg2;
    uint64_t sprg3;
    uint64_t ppr;

    int64_t tb_offset;
};

void spapr_register_nested(void);
void spapr_exit_nested(PowerPCCPU *cpu, int excp);

#endif /* HW_SPAPR_NESTED_H */
