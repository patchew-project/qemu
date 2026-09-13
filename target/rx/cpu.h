/*
 *  RX emulation definition
 *
 *  Copyright (c) 2019 Yoshinori Sato
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

#ifndef RX_CPU_H
#define RX_CPU_H

#include "qemu/bitops.h"
#include "hw/core/registerfields.h"
#include "cpu-qom.h"

#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-interrupt.h"
#include "qemu/cpu-float.h"

#ifdef CONFIG_USER_ONLY
#error "RX does not support user mode emulation"
#endif

/* PSW define */
REG32(PSW, 0)
FIELD(PSW, C, 0, 1)
FIELD(PSW, Z, 1, 1)
FIELD(PSW, S, 2, 1)
FIELD(PSW, O, 3, 1)
FIELD(PSW, I, 16, 1)
FIELD(PSW, U, 17, 1)
FIELD(PSW, PM, 20, 1)
FIELD(PSW, IPL, 24, 4)

/* FPSW define */
REG32(FPSW, 0)
FIELD(FPSW, RM, 0, 2)
FIELD(FPSW, CV, 2, 1)
FIELD(FPSW, CO, 3, 1)
FIELD(FPSW, CZ, 4, 1)
FIELD(FPSW, CU, 5, 1)
FIELD(FPSW, CX, 6, 1)
FIELD(FPSW, CE, 7, 1)
FIELD(FPSW, CAUSE, 2, 6)
FIELD(FPSW, DN, 8, 1)
FIELD(FPSW, EV, 10, 1)
FIELD(FPSW, EO, 11, 1)
FIELD(FPSW, EZ, 12, 1)
FIELD(FPSW, EU, 13, 1)
FIELD(FPSW, EX, 14, 1)
FIELD(FPSW, ENABLE, 10, 5)
FIELD(FPSW, FV, 26, 1)
FIELD(FPSW, FO, 27, 1)
FIELD(FPSW, FZ, 28, 1)
FIELD(FPSW, FU, 29, 1)
FIELD(FPSW, FX, 30, 1)
FIELD(FPSW, FLAGS, 26, 4)
FIELD(FPSW, FS, 31, 1)

/* DPSW define. The DPFPU status word mirrors the FPSW layout exactly. */
REG32(DPSW, 0)
FIELD(DPSW, DRM, 0, 2)
FIELD(DPSW, DCV, 2, 1)
FIELD(DPSW, DCO, 3, 1)
FIELD(DPSW, DCZ, 4, 1)
FIELD(DPSW, DCU, 5, 1)
FIELD(DPSW, DCX, 6, 1)
FIELD(DPSW, DCE, 7, 1)
FIELD(DPSW, CAUSE, 2, 6)
FIELD(DPSW, DDN, 8, 1)
FIELD(DPSW, DEV, 10, 1)
FIELD(DPSW, DEO, 11, 1)
FIELD(DPSW, DEZ, 12, 1)
FIELD(DPSW, DEU, 13, 1)
FIELD(DPSW, DEX, 14, 1)
FIELD(DPSW, ENABLE, 10, 5)
FIELD(DPSW, DFV, 26, 1)
FIELD(DPSW, DFO, 27, 1)
FIELD(DPSW, DFZ, 28, 1)
FIELD(DPSW, DFU, 29, 1)
FIELD(DPSW, DFX, 30, 1)
/* DFS is the OR of DFV, DFO, DFZ and DFU only; DFX is not included. */
FIELD(DPSW, FLAGS, 26, 4)
FIELD(DPSW, DFS, 31, 1)

/*
 * Writable bits of DPSW: DRM, the DC* causes, DDN, the DE* enables and the
 * DF* flags. The reserved bits and the read-only DFS summary are masked out.
 */
#define RX_DPSW_WRITE_MASK 0x7c007dff
/* The DC* cause bits, which MVTDC can only clear, never set. */
#define RX_DPSW_CAUSE_MASK 0x000000fc

/* DECNT define: DP exception information preservation. */
REG32(DECNT, 0)
FIELD(DECNT, EHM, 0, 1)
FIELD(DECNT, EHS, 16, 1)
#define RX_DECNT_RESET 0x00000001   /* EHM = 1 out of reset */

/* DCMR holds only the RES bit; everything else reads as 0. */
#define RX_DCMR_WRITE_MASK 0x00000001

enum {
    NUM_REGS = 16,
    /* RXv3 DPFPU: DR0-DR15 are dedicated 64-bit registers, not GPR pairs. */
    NUM_DREGS = 16,
    /* RXv3 DPFPU control registers: DPSW, DCMR, DECNT, DEPC. */
    NUM_DCREGS = 4,
    /*
     * RXv3 register bank save function. The banks are internal CPU state
     * reachable only through the SAVE and RSTR instructions, so no memory
     * layout is involved. How many banks exist is implementation defined,
     * so the per-model count lives in RXCPUClass::num_save_banks and this
     * is only the storage ceiling. RX72M/RX72N/RX72T provide 16, selected
     * by bank numbers 0-15.
     */
    RX_MAX_SAVE_BANKS = 16,
};

/* RXv3 DPFPU control register numbers (MVFDC/MVTDC/DPUSHM.L/DPOPM.L). */
enum {
    RX_DCR_DPSW  = 0,
    RX_DCR_DCMR  = 1,
    RX_DCR_DECNT = 2,
    RX_DCR_DEPC  = 3,
};

/*
 * DCMR.RES, bit 0, holds the result of the last DCMP; MVFDR copies it into
 * the PSW Z flag.
 */
#define RX_DCMR_RES_BIT 0

/*
 * DCMP condition field. The four documented mnemonics (un, eq, lt, le) are
 * a three bit mask of the relations that make DCMR.RES true, which is why
 * le == lt | eq == 6.
 */
#define RX_DCMP_UN  1
#define RX_DCMP_EQ  2
#define RX_DCMP_LT  4

/*
 * Instruction set architecture revision implemented by a CPU model.
 * Later revisions are supersets of earlier ones, so instruction gating is a
 * simple >= comparison.
 */
typedef enum RXISAVersion {
    RX_ISA_V1 = 1,
    RX_ISA_V2 = 2,
    RX_ISA_V3 = 3,
} RXISAVersion;

/*
 * One save register bank, written by SAVE and read back by RSTR. A bank
 * holds R1-R15 (R0/SP is excluded), the USP, the FPSW and the accumulators.
 * QEMU models a single accumulator, so only ACC0 is covered here.
 */
typedef struct RXSaveBank {
    uint32_t regs[NUM_REGS];    /* [0] unused: R0 is not banked */
    uint32_t usp;
    uint32_t fpsw;
    uint64_t acc;
} RXSaveBank;

typedef struct CPUArchState {
    /* CPU registers */
    uint32_t regs[NUM_REGS];    /* general registers */
    uint32_t psw_o;             /* O bit of status register */
    uint32_t psw_s;             /* S bit of status register */
    uint32_t psw_z;             /* Z bit of status register */
    uint32_t psw_c;             /* C bit of status register */
    uint32_t psw_u;
    uint32_t psw_i;
    uint32_t psw_pm;
    uint32_t psw_ipl;
    uint32_t bpsw;              /* backup status */
    uint32_t bpc;               /* backup pc */
    uint32_t isp;               /* global base register */
    uint32_t usp;               /* vector base register */
    uint32_t pc;                /* program counter */
    uint32_t intb;              /* interrupt vector */
    uint32_t extb;              /* exception vector table base */
    uint32_t fintv;
    uint32_t fpsw;
    uint64_t acc;
    uint64_t acc1;
    uint32_t acc_guard[2]; /* Sign-extended 8-bit RXv2 guard fields. */

    /* RXv3 double-precision FPU (DPFPU) */
    uint64_t dr[NUM_DREGS];     /* DR0-DR15 */
    uint32_t dcr[NUM_DCREGS];   /* DPSW, DCMR, DECNT, DEPC */

    /* RXv3 register bank save function (SAVE/RSTR) */
    RXSaveBank bank[RX_MAX_SAVE_BANKS];

    /* Fields up to this point are cleared by a CPU reset */
    struct {} end_reset_fields;

    /* Internal use */
    uint32_t in_sleep;
    uint32_t req_irq;           /* Requested interrupt no (hard) */
    uint32_t req_ipl;           /* Requested interrupt level */
    uint32_t ack_irq;           /* execute irq */
    uint32_t ack_ipl;           /* execute ipl */
    float_status fp_status;
    float_status dp_status;
    qemu_irq ack;               /* Interrupt acknowledge */
} CPURXState;

/*
 * RXCPU:
 * @env: #CPURXState
 *
 * A RX CPU
 */
struct ArchCPU {
    CPUState parent_obj;

    CPURXState env;
};

/*
 * RXCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A RX CPU model.
 */
struct RXCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;

    /* Instruction set revision this model implements. */
    RXISAVersion isa_version;

    /*
     * Number of save register banks reachable by SAVE/RSTR, or 0 on models
     * without the register bank save function. Must not exceed
     * RX_MAX_SAVE_BANKS.
     */
    uint32_t num_save_banks;
};

#define CPU_RESOLVING_TYPE TYPE_RX_CPU

const char *rx_crname(uint8_t cr);
void rx_cpu_do_interrupt(CPUState *cpu);
bool rx_cpu_exec_interrupt(CPUState *cpu, int int_req);
hwaddr rx_cpu_get_phys_addr_debug(CPUState *cpu, vaddr addr);
void rx_cpu_dump_state(CPUState *cpu, FILE *f, int flags);
int rx_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int rx_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

void rx_translate_init(void);
void rx_translate_code(CPUState *cs, TranslationBlock *tb,
                       int *max_insns, vaddr pc, void *host_pc);
void rx_cpu_unpack_psw(CPURXState *env, uint32_t psw, int rte);

#define CPU_INTERRUPT_SOFT CPU_INTERRUPT_TGT_INT_0
#define CPU_INTERRUPT_FIR  CPU_INTERRUPT_TGT_INT_1

#define RX_CPU_IRQ 0
#define RX_CPU_FIR 1

static inline uint32_t rx_cpu_pack_psw(CPURXState *env)
{
    uint32_t psw = 0;
    psw = FIELD_DP32(psw, PSW, IPL, env->psw_ipl);
    psw = FIELD_DP32(psw, PSW, PM,  env->psw_pm);
    psw = FIELD_DP32(psw, PSW, U,   env->psw_u);
    psw = FIELD_DP32(psw, PSW, I,   env->psw_i);
    psw = FIELD_DP32(psw, PSW, O,   env->psw_o >> 31);
    psw = FIELD_DP32(psw, PSW, S,   env->psw_s >> 31);
    psw = FIELD_DP32(psw, PSW, Z,   env->psw_z == 0);
    psw = FIELD_DP32(psw, PSW, C,   env->psw_c);
    return psw;
}

#endif /* RX_CPU_H */
