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

#ifndef CPU_ARC_H
#define CPU_ARC_H

#include "cpu-qom.h"
#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"

#include "target/arc/arc-common.h"
#include "target/arc/mmu.h"
#include "target/arc/mpu.h"
#include "target/arc/cache.h"

#define ARC_CPU_TYPE_SUFFIX "-" TYPE_ARC_CPU
#define ARC_CPU_TYPE_NAME(model) model ARC_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_ARC_CPU

#define MMU_IDX       0

#define PHYS_BASE_RAM 0x00000000
#define VIRT_BASE_RAM 0x00000000

enum arc_features {
    ARC_FEATURE_ARC5,
    ARC_FEATURE_ARC600,
    ARC_FEATURE_ARC700,
    no_features,
};

enum arc_endianess {
    ARC_ENDIANNESS_LE = 0,
    ARC_ENDIANNESS_BE,
};

/* U-Boot - kernel ABI */
#define ARC_UBOOT_CMDLINE 1
#define ARC_UBOOT_DTB     2

enum gdb_regs {
    GDB_REG_0 = 0,
    GDB_REG_1,
    GDB_REG_2,
    GDB_REG_3,
    GDB_REG_4,
    GDB_REG_5,
    GDB_REG_6,
    GDB_REG_7,
    GDB_REG_8,
    GDB_REG_9,
    GDB_REG_10,
    GDB_REG_11,
    GDB_REG_12,
    GDB_REG_13,
    GDB_REG_14,
    GDB_REG_15,
    GDB_REG_16,
    GDB_REG_17,
    GDB_REG_18,
    GDB_REG_19,
    GDB_REG_20,
    GDB_REG_21,
    GDB_REG_22,
    GDB_REG_23,
    GDB_REG_24,
    GDB_REG_25,
    GDB_REG_26,         /* GP                         */
    GDB_REG_27,         /* FP                         */
    GDB_REG_28,         /* SP                         */
    GDB_REG_29,         /* ILINK                      */
    GDB_REG_30,         /* R30                        */
    GDB_REG_31,         /* BLINK                      */
    GDB_REG_58,         /* little_endian? ACCL : ACCH */
    GDB_REG_59,         /* little_endian? ACCH : ACCL */
    GDB_REG_60,         /* LP                         */
    GDB_REG_63,         /* Immediate                  */
    GDB_REG_LAST
};

enum gdb_aux_min_regs {
    GDB_AUX_MIN_REG_PC = 0, /* program counter */
    GDB_AUX_MIN_REG_LPS,    /* loop body start */
    GDB_AUX_MIN_REG_LPE,    /* loop body end   */
    GDB_AUX_MIN_REG_STATUS, /* status flag     */
    GDB_AUX_MIN_REG_LAST
};

enum gdb_aux_other_regs {
    /* builds */
    GDB_AUX_OTHER_REG_TIMER_BUILD = 0,  /* timer build                */
    GDB_AUX_OTHER_REG_IRQ_BUILD,        /* irq build                  */
    GDB_AUX_OTHER_REG_MPY_BUILD,        /* multiply configuration     */
    GDB_AUX_OTHER_REG_VECBASE_BUILD,    /* vector base address config */
    GDB_AUX_OTHER_REG_ISA_CONFIG,       /* isa config                 */
    /* timers */
    GDB_AUX_OTHER_REG_TIMER_CNT0,       /* timer 0 counter */
    GDB_AUX_OTHER_REG_TIMER_CTRL0,      /* timer 0 control */
    GDB_AUX_OTHER_REG_TIMER_LIM0,       /* timer 0 limit   */
    GDB_AUX_OTHER_REG_TIMER_CNT1,       /* timer 1 counter */
    GDB_AUX_OTHER_REG_TIMER_CTRL1,      /* timer 1 control */
    GDB_AUX_OTHER_REG_TIMER_LIM1,       /* timer 1 limit   */
    /* mmu */
    GDB_AUX_OTHER_REG_PID,              /* process identity  */
    GDB_AUX_OTHER_REG_TLBPD0,           /* page descriptor 0 */
    GDB_AUX_OTHER_REG_TLBPD1,           /* page descriptor 1 */
    GDB_AUX_OTHER_REG_TLB_INDEX,        /* tlb index         */
    GDB_AUX_OTHER_REG_TLB_CMD,          /* tlb command       */
    /* mpu */
    GDB_AUX_OTHER_REG_MPU_BUILD,        /* MPU build           */
    GDB_AUX_OTHER_REG_MPU_EN,           /* MPU enable          */
    GDB_AUX_OTHER_REG_MPU_ECR,          /* MPU exception cause */
    GDB_AUX_OTHER_REG_MPU_BASE0,        /* MPU base 0          */
    GDB_AUX_OTHER_REG_MPU_BASE1,        /* MPU base 1          */
    GDB_AUX_OTHER_REG_MPU_BASE2,        /* MPU base 2          */
    GDB_AUX_OTHER_REG_MPU_BASE3,        /* MPU base 3          */
    GDB_AUX_OTHER_REG_MPU_BASE4,        /* MPU base 4          */
    GDB_AUX_OTHER_REG_MPU_BASE5,        /* MPU base 5          */
    GDB_AUX_OTHER_REG_MPU_BASE6,        /* MPU base 6          */
    GDB_AUX_OTHER_REG_MPU_BASE7,        /* MPU base 7          */
    GDB_AUX_OTHER_REG_MPU_BASE8,        /* MPU base 8          */
    GDB_AUX_OTHER_REG_MPU_BASE9,        /* MPU base 9          */
    GDB_AUX_OTHER_REG_MPU_BASE10,       /* MPU base 10         */
    GDB_AUX_OTHER_REG_MPU_BASE11,       /* MPU base 11         */
    GDB_AUX_OTHER_REG_MPU_BASE12,       /* MPU base 12         */
    GDB_AUX_OTHER_REG_MPU_BASE13,       /* MPU base 13         */
    GDB_AUX_OTHER_REG_MPU_BASE14,       /* MPU base 14         */
    GDB_AUX_OTHER_REG_MPU_BASE15,       /* MPU base 15         */
    GDB_AUX_OTHER_REG_MPU_PERM0,        /* MPU permission 0    */
    GDB_AUX_OTHER_REG_MPU_PERM1,        /* MPU permission 1    */
    GDB_AUX_OTHER_REG_MPU_PERM2,        /* MPU permission 2    */
    GDB_AUX_OTHER_REG_MPU_PERM3,        /* MPU permission 3    */
    GDB_AUX_OTHER_REG_MPU_PERM4,        /* MPU permission 4    */
    GDB_AUX_OTHER_REG_MPU_PERM5,        /* MPU permission 5    */
    GDB_AUX_OTHER_REG_MPU_PERM6,        /* MPU permission 6    */
    GDB_AUX_OTHER_REG_MPU_PERM7,        /* MPU permission 7    */
    GDB_AUX_OTHER_REG_MPU_PERM8,        /* MPU permission 8    */
    GDB_AUX_OTHER_REG_MPU_PERM9,        /* MPU permission 9    */
    GDB_AUX_OTHER_REG_MPU_PERM10,       /* MPU permission 10   */
    GDB_AUX_OTHER_REG_MPU_PERM11,       /* MPU permission 11   */
    GDB_AUX_OTHER_REG_MPU_PERM12,       /* MPU permission 12   */
    GDB_AUX_OTHER_REG_MPU_PERM13,       /* MPU permission 13   */
    GDB_AUX_OTHER_REG_MPU_PERM14,       /* MPU permission 14   */
    GDB_AUX_OTHER_REG_MPU_PERM15,       /* MPU permission 15   */
    /* excpetions */
    GDB_AUX_OTHER_REG_ERSTATUS,         /* exception return status  */
    GDB_AUX_OTHER_REG_ERBTA,            /* exception return BTA     */
    GDB_AUX_OTHER_REG_ECR,              /* exception cause register */
    GDB_AUX_OTHER_REG_ERET,             /* exception return address */
    GDB_AUX_OTHER_REG_EFA,              /* exception fault address  */
    /* irq */
    GDB_AUX_OTHER_REG_ICAUSE,           /* interrupt cause        */
    GDB_AUX_OTHER_REG_IRQ_CTRL,         /* context saving control */
    GDB_AUX_OTHER_REG_IRQ_ACT,          /* active                 */
    GDB_AUX_OTHER_REG_IRQ_PRIO_PEND,    /* priority pending       */
    GDB_AUX_OTHER_REG_IRQ_HINT,         /* hint                   */
    GDB_AUX_OTHER_REG_IRQ_SELECT,       /* select                 */
    GDB_AUX_OTHER_REG_IRQ_ENABLE,       /* enable                 */
    GDB_AUX_OTHER_REG_IRQ_TRIGGER,      /* trigger                */
    GDB_AUX_OTHER_REG_IRQ_STATUS,       /* status                 */
    GDB_AUX_OTHER_REG_IRQ_PULSE,        /* pulse cancel           */
    GDB_AUX_OTHER_REG_IRQ_PENDING,      /* pending                */
    GDB_AUX_OTHER_REG_IRQ_PRIO,         /* priority               */
    /* miscellaneous */
    GDB_AUX_OTHER_REG_BTA,              /* branch target address */

    GDB_AUX_OTHER_REG_LAST
};

#define CPU_GP(env)     ((env)->r[26])
#define CPU_FP(env)     ((env)->r[27])
#define CPU_SP(env)     ((env)->r[28])
#define CPU_ILINK(env)  ((env)->r[29])
#define CPU_ILINK1(env) ((env)->r[29])
#define CPU_ILINK2(env) ((env)->r[30])
#define CPU_BLINK(env)  ((env)->r[31])
#define CPU_LP(env)     ((env)->r[60])
#define CPU_IMM(env)    ((env)->r[62])
#define CPU_PCL(env)    ((env)->r[63])

enum exception_code_list {
    EXCP_NO_EXCEPTION = -1,
    EXCP_RESET = 0,
    EXCP_MEMORY_ERROR,
    EXCP_INST_ERROR,
    EXCP_MACHINE_CHECK,
    EXCP_TLB_MISS_I,
    EXCP_TLB_MISS_D,
    EXCP_PROTV,
    EXCP_PRIVILEGEV,
    EXCP_SWI,
    EXCP_TRAP,
    EXCP_EXTENSION,
    EXCP_DIVZERO,
    EXCP_DCERROR,
    EXCP_MISALIGNED,
    EXCP_IRQ,
    EXCP_LPEND_REACHED = 9000,
    EXCP_FAKE
};

typedef struct status_register {
    uint32_t Hf;     /* halt                    */
    uint32_t Ef;     /* irq priority treshold. */
    uint32_t AEf;
    uint32_t DEf;
    uint32_t Uf;
    uint32_t Vf;     /*  overflow                */
    uint32_t Cf;     /*  carry                   */
    uint32_t Nf;     /*  negative                */
    uint32_t Zf;     /*  zero                    */
    uint32_t Lf;
    uint32_t DZf;
    uint32_t SCf;
    uint32_t ESf;
    uint32_t RBf;
    uint32_t ADf;
    uint32_t USf;
    uint32_t IEf;

    /* Reserved bits */

    /* Next instruction is a delayslot instruction */
    uint32_t is_delay_slot_instruction;
} status_t;

/* ARC processor timer module. */
typedef struct {
    /*
     * TODO: This volatile is needed to pass RTC tests. We need to
     * verify why.
     */
    volatile uint32_t T_Cntrl;
    volatile uint32_t T_Limit;
    volatile uint64_t last_clk;
} arc_timer_t;

/* ARC PIC interrupt bancked regs. */
typedef struct {
    uint32_t priority;
    uint32_t trigger;
    uint32_t pulse_cancel;
    uint32_t enable;
    uint32_t pending;
    uint32_t status;
} arc_irq_t;

typedef struct CPUARCState {
    uint32_t        r[64];

    status_t stat, stat_l1, stat_er;

    struct {
        uint32_t    S2;
        uint32_t    S1;
        uint32_t    CS;
    } macmod;

    uint32_t intvec;

    uint32_t eret;
    uint32_t erbta;
    uint32_t ecr;
    uint32_t efa;
    uint32_t bta;
    uint32_t bta_l1;
    uint32_t bta_l2;

    uint32_t pc;     /*  program counter         */
    uint32_t lps;    /*  loops start             */
    uint32_t lpe;    /*  loops end               */

    uint32_t npc;    /* required for LP - zero overhead loops. */

    uint32_t lock_lf_var;

    struct {
        uint32_t LD;     /*  load pending bit        */
        uint32_t SH;     /*  self halt               */
        uint32_t BH;     /*  breakpoint halt         */
        uint32_t UB;     /*  user mode break enabled */
        uint32_t ZZ;     /*  sleep mode              */
        uint32_t RA;     /*  reset applied           */
        uint32_t IS;     /*  single instruction step */
        uint32_t FH;     /*  force halt              */
        uint32_t SS;     /*  single step             */
    } debug;

#define TMR_IE  (1 << 0)
#define TMR_NH  (1 << 1)
#define TMR_W   (1 << 2)
#define TMR_IP  (1 << 3)
#define TMR_PD  (1 << 4)
    arc_timer_t timer[2];    /* ARC CPU-Timer 0/1 */

    arc_irq_t irq_bank[256]; /* IRQ register bank */
    uint32_t irq_select;     /* AUX register */
    uint32_t aux_irq_act;    /* AUX register */
    uint32_t irq_priority_pending; /* AUX register */
    uint32_t icause[16];     /* Banked cause register */
    uint32_t aux_irq_hint;   /* AUX register, used to trigger soft irq */
    uint32_t aux_user_sp;
    uint32_t aux_irq_ctrl;
    uint32_t aux_rtc_ctrl;
    uint32_t aux_rtc_low;
    uint32_t aux_rtc_high;

    /* Fields required by exception handling. */
    uint32_t causecode;
    uint32_t param;

    struct arc_mmu mmu;       /* mmu.h */
    ARCMPU mpu;               /* mpu.h */
    struct arc_cache cache;   /* cache.h */

    /* used for propagatinng "hostpc/return address" to sub-functions */
    uintptr_t host_pc;

    bool      stopped;

    /* Fields up to this point are cleared by a CPU reset */
    struct {} end_reset_fields;

    /* Fields after this point are preserved across CPU reset. */
    uint64_t features;
    uint32_t family;

    uint32_t freq_hz; /* CPU frequency in hz, needed for timers. */
    uint64_t last_clk_rtc;

    void *irq[256];
    QEMUTimer *cpu_timer[2]; /* Internal timer. */
    QEMUTimer *cpu_rtc;      /* Internal RTC. */

    /* Build AUX regs. */
#define TIMER0_IRQ 16
#define TIMER1_IRQ 17
#define TB_T0  (1 << 8)
#define TB_T1  (1 << 9)
#define TB_RTC (1 << 10)
#define TB_P0_MSK (0x0f0000)
#define TB_P1_MSK (0xf00000)
    uint32_t timer_build;   /* Timer configuration AUX register. */
    uint32_t irq_build;     /* Interrupt Build Configuration Register. */
    uint32_t vecbase_build; /* Interrupt Vector Base Address Configuration. */
    uint32_t mpy_build;     /* Multiply configuration register. */
    uint32_t isa_config;    /* Instruction Set Configuration Register. */

    const struct arc_boot_info *boot_info;
} CPUARCState;

/*
 * ArcCPU:
 * @env: #CPUMBState
 *
 * An ARC CPU.
 */
struct ARCCPU {
  /*< private >*/
  CPUState parent_obj;

  /*< public >*/

  /* ARC Configuration Settings. */
  struct {
    uint32_t addr_size;
    bool     aps_feature;
    bool     byte_order;
    bool     bitscan_option;
    uint32_t br_bc_entries;
    uint32_t br_pt_entries;
    bool     br_bc_full_tag;
    uint8_t  br_rs_entries;
    uint32_t br_bc_tag_size;
    uint8_t  br_tosq_entries;
    uint8_t  br_fb_entries;
    bool     code_density;
    bool     code_protect;
    uint8_t  dccm_mem_cycles;
    bool     dccm_posedge;
    uint8_t  dccm_mem_bancks;
    uint8_t  dc_mem_cycles;
    bool     dc_posedge;
    bool     dmp_unaligned;
    bool     ecc_exception;
    uint32_t external_interrupts;
    uint8_t  ecc_option;
    bool     firq_option;
    bool     fpu_dp_option;
    bool     fpu_fma_option;
    bool     fpu_div_option;
    bool     has_actionpoints;
    bool     has_fpu;
    bool     has_interrupts;
    bool     has_mmu;
    bool     has_mpu;
    bool     has_timer_0;
    bool     has_timer_1;
    bool     has_pct;
    bool     has_rtt;
    bool     has_smart;
    uint32_t intvbase_preset;
    uint32_t lpc_size;
    uint8_t  mpu_num_regions;
    uint8_t  mpy_option;
    uint32_t mmu_page_size_sel0;
    uint32_t mmu_page_size_sel1;
    uint32_t mmu_pae_enabled;
    uint32_t ntlb_num_entries;
    uint32_t num_actionpoints;
    uint32_t number_of_interrupts;
    uint32_t number_of_levels;
    uint32_t pct_counters;
    uint32_t pct_interrupt;
    uint32_t pc_size;
    uint32_t rgf_num_regs;
    uint32_t rgf_banked_regs;
    uint32_t rgf_num_banks;
    bool     rtc_option;
    uint32_t rtt_feature_level;
    bool     stack_checking;
    bool     swap_option;
    uint32_t smar_stack_entries;
    uint32_t smart_implementation;
    uint32_t stlb_num_entries;
    uint32_t slc_size;
    uint32_t slc_line_size;
    uint32_t slc_ways;
    uint32_t slc_tag_banks;
    uint32_t slc_tram_delay;
    uint32_t slc_dbank_width;
    uint32_t slc_data_banks;
    uint32_t slc_dram_delay;
    bool     slc_mem_bus_width;
    uint32_t slc_ecc_option;
    bool     slc_data_halfcycle_steal;
    bool     slc_data_add_pre_pipeline;
    bool     uaux_option;
    uint32_t freq_hz; /* CPU frequency in hz, needed for timers. */
  } cfg;

  CPUNegativeOffsetState neg;
  CPUARCState env;
};

/* are we in user mode? */
static inline bool is_user_mode(const CPUARCState *env)
{
    return env->stat.Uf != false;
}

static inline int arc_feature(const CPUARCState *env, int feature)
{
    return (env->features & (1U << feature)) != 0;
}

static inline void  arc_set_feature(CPUARCState *env, int feature)
{
    env->features |= (1U << feature);
}

#define cpu_list            arc_cpu_list
#define cpu_signal_handler  cpu_arc_signal_handler
#define cpu_init(cpu_model) cpu_generic_init(TYPE_ARC_CPU, cpu_model)

typedef CPUARCState CPUArchState;
typedef ARCCPU ArchCPU;

#include "exec/cpu-all.h"

static inline int cpu_mmu_index(const CPUARCState *env, bool ifetch)
{
    return env->stat.Uf != 0 ? 1 : 0;
}

static inline void cpu_get_tb_cpu_state(CPUARCState *env, target_ulong *pc,
                                        target_ulong *cs_base,
                                        uint32_t *pflags)
{
    *pc = env->pc;
    *cs_base = 0;
#ifdef CONFIG_USER_ONLY
    *pflags = TB_FLAGS_FP_ENABLE;
#else
    *pflags = cpu_mmu_index(env, 0);
#endif
}

static inline int cpu_interrupts_enabled(const CPUARCState *env)
{
    return env->stat.IEf;
}

void arc_translate_init(void);

void arc_cpu_list(void);
int cpu_arc_exec(CPUState *cpu);
int cpu_arc_signal_handler(int host_signum, void *pinfo, void *puc);
bool arc_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr);
int arc_cpu_memory_rw_debug(CPUState *cs, vaddr address, uint8_t *buf,
                            int len, bool is_write);
void arc_cpu_do_interrupt(CPUState *cpu);

void arc_cpu_dump_state(CPUState *cs, FILE *f, int flags);
hwaddr arc_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int arc_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int arc_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

void QEMU_NORETURN arc_raise_exception(CPUARCState *env, int32_t excp_idx);

#include "exec/cpu-all.h"

#endif /* !defined (CPU_ARC_H) */
